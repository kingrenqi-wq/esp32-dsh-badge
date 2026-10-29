import assert from 'node:assert/strict'
import { decide } from '../bridge/dsh-state.mjs'
const now=1000000
const user={type:'agent/inbox/spliced',time:now-1000,data:{turn:1}}
assert.equal(decide([user],now).state,'think')
assert.equal(decide([{...user,time:now-130000}],now).state,'idle')
assert.equal(decide([],now).state,'idle')
assert.equal(decide([user,{type:'tool/result',time:now-500,data:{message:{isError:true}}}],now).state,'error')
const done={type:'assistant/message',time:now-4000,data:{turn:1,message:{content:[{type:'text',text:'done'}]}}}
assert.equal(decide([done],now).state,'done')
assert.equal(decide([{...done,time:now-10000}],now).state,'idle')
assert.equal(decide([done,user],now).state,'think')
assert.equal(decide([{...done,data:{message:{toolCalls:[{}]}}}],now).state,'think')
console.log('PASS: first turn, empty/stale sessions, tool error, done/idle, next turn, tool calls')
