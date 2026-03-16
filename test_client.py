import socket, time

s = socket.socket()
s.connect(('127.0.0.1', 7000))
s.settimeout(2)

def recv_all():
    data = b''
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
    except:
        pass
    return data.decode(errors='replace').strip()

def cmd(msg):
    s.sendall((msg + '\r\n').encode())
    time.sleep(0.4)
    resp = recv_all()
    print(f'>> {msg}')
    print(f'   {resp}\n')
    return resp

time.sleep(0.3)
welcome = recv_all()
print(f'[서버] {welcome}\n')

cmd('CONNECT hero')
cmd('LOOK')
cmd('ATTACK 2000')           # 거리 초과
cmd('MOVE 20 20')            # Goblin(20,20) 위치로 이동
cmd('LOOK')                  # [IN RANGE] 확인
cmd('ATTACK 2000 damage:25') # 공격 성공
cmd('MOVE 50 50')            # Orc(50,50) 위치로 이동
cmd('ATTACK 2001 damage:40') # 공격 성공
cmd('MOVE 80 80')            # Dark Knight(80,80) 위치로 이동
cmd('ATTACK 2002 damage:15') # 공격 성공
cmd('STATUS')
cmd('QUIT')

s.close()
