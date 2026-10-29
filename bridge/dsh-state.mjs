// dsh-state.mjs —— 从 DSH 会话文件里判读"当前轮是否在进行中"
//
// 原理：~/.dsh/sessions/<ws>/session-*/session.v4.jsonl.zstd 里每条事件是
// 一个独立的 zstd 帧（可用 0x28B5 2FFD 魔数切分）。事件类型有：
//   agent/inbox/spliced（用户发消息）  step/start（模型开始一步）
//   assistant/message（这一步的输出）  tool/result（工具返回）  job/settled
//
// 判定：
//   最新事件是 用户消息 / step/start / tool/result          → think
//   最新事件是 assistant/message 且里面还带工具调用          → think
//   最新事件是 assistant/message 且是纯文字（本轮最终答复）  → done（4 秒后 idle）
//
// 输出（每 500ms 一行）：think|<本轮秒数>|<turn号>
// 用法：node dsh-state.mjs [会话文件路径]

import fs from 'node:fs'
import path from 'node:path'
import zlib from 'node:zlib'
import os from 'node:os'
import { fileURLToPath } from 'node:url'

const MAGIC = Buffer.from([0x28, 0xb5, 0x2f, 0xfd])
const DONE_HOLD_MS = 4000
const STALE_MS = 120000   // 2 分钟没有任何新事件 => 不再算“思考中”
const SESSIONS_ROOT = path.join(os.homedir(), '.dsh', 'sessions')
const DAY_CACHE = path.join(os.homedir(), '.config', 'dsh-day-cache.json')
const GAP_MS = 5 * 60 * 1000      // 相邻事件超过 5 分钟算中断，不计入专注时长

// ── 今日统计：从会话文件本身算，不依赖桥从什么时候开始跑 ──────────────
let dayCache = {}
try { dayCache = JSON.parse(fs.readFileSync(DAY_CACHE, 'utf8')) } catch {}

function hourOf(ms) { return new Date(ms).getHours() }

function fileStats(file) {
  const st = fs.statSync(file)
  const key = file
  const hit = dayCache[key]
  if (hit && hit.mtime === st.mtimeMs && hit.size === st.size) return hit
  let buf
  try { buf = fs.readFileSync(file) } catch { return null }
  const offs = frameOffsets(buf)
  const times = []
  for (const s of offs) {
    const e = (offs.indexOf(s) + 1 < offs.length) ? offs[offs.indexOf(s) + 1] : buf.length
    try {
      const txt = zlib.zstdDecompressSync(buf.subarray(s, e)).toString('utf8')
      for (const line of txt.trim().split('\n')) {
        try { const r = JSON.parse(line); if (r?.time) times.push(r.time) } catch {}
      }
    } catch {}
  }
  times.sort((a, b) => a - b)
  const rec = { mtime: st.mtimeMs, size: st.size, times: times.slice(-20000) }
  dayCache[key] = rec
  return rec
}

// 最近 4 个自然周（周一起）每天的活跃秒数，供「近四周」热力日历用
function daySeries() {
  const today = new Date(); today.setHours(0, 0, 0, 0)
  const dow = (today.getDay() + 6) % 7                // 0=周一
  const start = new Date(today.getTime() - (21 + dow) * 86400000)   // 三周前的周一
  const t0 = start.getTime()
  const daily = new Array(28).fill(0)
  const merged = []
  for (const f of allSessionFiles()) {
    let st
    try { st = fs.statSync(f) } catch { continue }
    if (st.mtimeMs < t0) continue
    const rec = fileStats(f)
    if (rec) for (const t of rec.times) if (t >= t0) merged.push(t)
  }
  merged.sort((a, b) => a - b)
  for (let i = 1; i < merged.length; i++) {
    const gap = merged[i] - merged[i - 1]
    if (gap <= 0 || gap > GAP_MS) continue
    const idx = Math.floor((merged[i - 1] - t0) / 86400000)
    if (idx >= 0 && idx < 28) daily[idx] += gap / 1000
  }
  const nowIdx = Math.floor((Date.now() - t0) / 86400000)
  return daily.map((v, i) => (i > nowIdx ? 0 : Math.round(v)))
}

function dayActive() {
  const today = new Date(); today.setHours(0, 0, 0, 0)
  const t0 = today.getTime()
  const perHour = new Array(24).fill(0)
  const merged = []
  let sessions = 0
  for (const f of allSessionFiles()) {
    let st
    try { st = fs.statSync(f) } catch { continue }
    if (st.mtimeMs < t0) continue          // 今天没动过
    sessions++
    const rec = fileStats(f)
    if (rec) for (const t of rec.times) if (t >= t0) merged.push(t)
  }
  merged.sort((a, b) => a - b)
  for (let i = 1; i < merged.length; i++) {
    const gap = merged[i] - merged[i - 1]
    if (gap <= 0 || gap > GAP_MS) continue
    perHour[hourOf(merged[i - 1])] += gap / 1000
  }
  try { fs.writeFileSync(DAY_CACHE, JSON.stringify(dayCache)) } catch {}
  return { active: perHour.reduce((a, b) => a + b, 0), perHour, sessions }
}

function allSessionFiles() {
  const out = []
  let ws
  try { ws = fs.readdirSync(SESSIONS_ROOT) } catch { return out }
  for (const w of ws) {
    let entries
    try { entries = fs.readdirSync(path.join(SESSIONS_ROOT, w)) } catch { continue }
    for (const e of entries) {
      if (!e.startsWith('session-')) continue
      out.push(path.join(SESSIONS_ROOT, w, e, 'session.v4.jsonl.zstd'))
    }
  }
  return out
}

function newestSession() {
  let best = null
  let workspaces
  try { workspaces = fs.readdirSync(SESSIONS_ROOT) } catch { return null }
  for (const ws of workspaces) {
    const wsDir = path.join(SESSIONS_ROOT, ws)
    let entries
    try { entries = fs.readdirSync(wsDir) } catch { continue }
    for (const e of entries) {
      if (!e.startsWith('session-')) continue
      const f = path.join(wsDir, e, 'session.v4.jsonl.zstd')
      try {
        const m = fs.statSync(f).mtimeMs
        if (!best || m > best.mtime) best = { file: f, mtime: m }
      } catch {}
    }
  }
  return best
}

function frameOffsets(buf) {
  const out = []
  for (let i = 0; i <= buf.length - 4; i++) {
    if (buf[i] === MAGIC[0] && buf[i + 1] === MAGIC[1] && buf[i + 2] === MAGIC[2] && buf[i + 3] === MAGIC[3]) out.push(i)
  }
  return out
}

function readTail(file, want = 40) {
  const buf = fs.readFileSync(file)
  const offs = frameOffsets(buf)
  const out = []
  for (let k = Math.max(0, offs.length - want); k < offs.length; k++) {
    const s = offs[k]
    const e = k + 1 < offs.length ? offs[k + 1] : buf.length
    try {
      const txt = zlib.zstdDecompressSync(buf.subarray(s, e)).toString('utf8')
      for (const line of txt.trim().split('\n')) {
        try { out.push(JSON.parse(line)) } catch {}
      }
    } catch {}
  }
  return out
}

function hasToolCall(msg) {
  const c = Array.isArray(msg?.content) ? msg.content : []
  if (c.some((x) => String(x?.type || '').toLowerCase().includes('tool'))) return true
  if (Array.isArray(msg?.toolCalls) && msg.toolCalls.length) return true
  return false
}

export function decide(records, now) {
  // 以"最后一条 assistant 输出"为准：后面再跟 job/settled、tool/result 都不影响判断
  let lastAsst = null, lastStep = null, lastUser = null
  for (let i = records.length - 1; i >= 0; i--) {
    const r = records[i]
    if (!r?.type) continue
    if (!lastAsst && r.type === 'assistant/message') lastAsst = r
    if (!lastStep && r.type === 'step/start') lastStep = r
    if (!lastUser && r.type === 'agent/inbox/spliced') lastUser = r
  }

  // 事件流长时间没有新增（比如工具被中断、宿主卡住）→ 不再假装在思考
  const lastRecTime = records.reduce((m, r) => Math.max(m, r.time || 0), 0)
  if (now - lastRecTime > STALE_MS) return { state: 'idle', elapsed: 0, turn: 0 }

  const turn = lastAsst?.data?.turn || lastStep?.data?.turn || 0
  const startedAt = lastUser?.time || lastStep?.time || lastAsst?.time || now

  const newerStep = lastStep && (!lastAsst || lastStep.time > lastAsst.time)
  const newerUser = lastUser && (!lastAsst || lastUser.time > lastAsst.time)
  // 出错：最近 8 秒内有工具失败，且之后还没有新的 assistant 输出
  let lastErr = null
  for (let i = records.length - 1; i >= 0; i--) {
    const r = records[i]
    if (r?.type === 'tool/result' && r?.data?.message?.isError === true) { lastErr = r; break }
  }
  if (lastErr && (now - lastErr.time) < 8000 && (!lastAsst || lastAsst.time < lastErr.time)) {
    return { state: 'error', elapsed: 0, turn }
  }

  if (!lastAsst) return { state: (lastUser || lastStep) ? 'think' : 'idle', elapsed: Math.max(0, Math.round((now-startedAt)/1000)), turn }

  const textOnly = !hasToolCall(lastAsst?.data?.message)

  if (!textOnly || newerStep || newerUser) {
    return { state: 'think', elapsed: Math.round((now - startedAt) / 1000), turn }
  }

  // 用"最后一条记录的时间"而不是这条消息的时间：消息时间可能是这步开始时刻
  const lastWrite = records.reduce((m, r) => Math.max(m, r.time || 0), 0)
  const since = now - (lastWrite || lastAsst?.time || now)
  if (since < 2500) return { state: 'think', elapsed: Math.round((now - startedAt) / 1000), turn }
  if (since < 6800) return { state: 'done', elapsed: 0, turn }
  return { state: 'idle', elapsed: 0, turn }
}

export { dayActive, daySeries }

function tick(file) {
  if (!file) return 'error|0|0'
  try {
    const recs = readTail(file)
    const r = decide(recs, Date.now())
    return `${r.state}|${r.elapsed}|${r.turn}`
  } catch {
    return 'error|0|0'
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
const fixed = process.argv[2]
if (!fixed) {
  // 常驻模式：每 500ms 输出一行
  let day = { active: 0, perHour: new Array(24).fill(0), sessions: 0 }
  let series = new Array(28).fill(0)
  let dayAt = 0
  const refreshDay = () => {
    try { day = dayActive(); series = daySeries(); dayAt = Date.now() } catch {}
  }
  refreshDay()
  const timer = setInterval(() => {
    const s = newestSession()
    if (Date.now() - dayAt > 60000) refreshDay()
    const line = tick(s?.file)
    process.stdout.write(
      `${line}|${Math.round(day.active)}|${day.sessions}|${day.perHour.map(v => Math.round(v)).join(',')}` +
      `|${series.join(',')}\n`)
  }, 500)
} else {
  process.stdout.write(tick(fixed) + '\n')
}

}
