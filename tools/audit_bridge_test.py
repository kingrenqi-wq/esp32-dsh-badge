"""Offline regression tests for the deployed bridge's data and serial boundaries."""
import importlib.util, unittest, tempfile, sqlite3, pathlib, datetime, threading, time
from unittest.mock import patch
SOURCE=pathlib.Path.home()/'Documents/deepseek-harness/default-workspace/dsh-badge.py'
spec=importlib.util.spec_from_file_location('badge',SOURCE);b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
class Tests(unittest.TestCase):
 def test_notification_and_frame_budget(self):
  text=b.clean_text('天气|坏\n行\r'*100)
  self.assertLessEqual(len(text.encode()),80);self.assertNotIn('|',text);self.assertNotIn('\n',text)
  ext={k:99 for k in ['hum','wind','tmax','tmin','d2c','d2max','d2min','d3c','d3max','d3min','d4c','d4max','d4min','d5c','d5max','d5min','d6c','d6max','d6min']}
  ext.update(hr=','.join(['-99']*24),sr='06:30',ss='17:30')
  frame=b.make_frame('think',100,12345678.9,12345678.9,(2,-99,ext),([999999999999.99]*7,1000000000),dict(cpu=100,mem=100,dn=1e8,up=1e8,disk=999999),notification=text)
  self.assertEqual(len(frame.strip().split('|')),46);self.assertGreater(len(frame.encode()),319);self.assertLessEqual(len(frame.encode())-1,768)
 def test_missing_week_is_not_zero(self):
  frame=b.make_frame('idle',0,None,None,(None,None,{}),(None,None),{})
  fields=frame.rstrip('\n').split('|');self.assertEqual(fields[33],'');self.assertEqual(fields[34],'nan');self.assertEqual(len(fields),46)
 def test_cny_only_calendar_days(self):
  with tempfile.TemporaryDirectory() as tmp:
   db=str(pathlib.Path(tmp)/'ledger.sqlite');con=sqlite3.connect(db);con.execute('create table days(day text,currency text,cost real,records int)')
   today=datetime.date.today();con.executemany('insert into days values(?,?,?,?)',[(today.isoformat(),'CNY',3.25,7),(today.isoformat(),'USD',999,999),((today-datetime.timedelta(days=6)).isoformat(),'CNY',1.5,2),((today-datetime.timedelta(days=7)).isoformat(),'CNY',100,100)]);con.commit();con.close()
   with patch.object(b,'LEDGER',db):wk,count=b.week_stats()
   self.assertEqual(wk,[1.5,0,0,0,0,0,3.25]);self.assertEqual(count,7)
 def test_failed_ledger_missing(self):
  with patch.object(b,'LEDGER','/nonexistent/dsh-test.sqlite'):self.assertEqual(b.week_stats(),(None,None))
 def test_partial_serial_write(self):
  got=bytearray();calls=0
  def fake(fd,data):
   nonlocal calls
   calls+=1
   if calls==1:raise BlockingIOError()
   n=min(3,len(data));got.extend(data[:n]);return n
  with patch.object(b.os,'write',side_effect=fake),patch.object(b.select,'select',return_value=([],[99],[])):
   b.write_all(99,b'DSH|test\n')
  self.assertEqual(got,b'DSH|test\n')
 def test_slow_fetch_does_not_block_frame_generation(self):
  release=threading.Event();entered=threading.Event()
  def slow():entered.set();release.wait(2);return 5
  task=b.CachedTask(slow,60,None);entered.wait(1)
  try:
   start=time.monotonic()
   for _ in range(100):b.make_frame('idle',0,task.get(),None,(None,None,{}),(None,None),{})
   self.assertLess(time.monotonic()-start,.5)
  finally:task.stop.set();release.set();task.thread.join(1)
 def test_stalled_state_source_reports_error(self):
  from unittest.mock import Mock
  ns=b.NodeState.__new__(b.NodeState)
  ns.state='think';ns.updated_at=time.monotonic()-6;ns.next_spawn=time.monotonic()+2;ns.proc=Mock()
  ns.poll();self.assertEqual(ns.state,'error');ns.proc.kill.assert_called_once()
 def test_key_uses_capture_lock(self):
  class Handler:
   path='/key?k=L'
   def send_response(self,status):self.status=status
   def end_headers(self):pass
  h=Handler();b.SERIAL_FD['fd']=99
  try:
   with patch.object(b,'write_all') as write:
    with b.CAPTURE_LOCK:
     worker=threading.Thread(target=b.NotifyHandler.do_GET,args=(h,));worker.start();time.sleep(.03);write.assert_not_called()
    worker.join(1);write.assert_called_once_with(99,b'KEY L\n');self.assertEqual(h.status,204)
  finally:b.SERIAL_FD['fd']=None
if __name__=='__main__':unittest.main(verbosity=2)
