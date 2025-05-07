import os
import socket
import json
import errno
import signal
import sys
import fcntl
import time
import netifaces
from vosk import Model, KaldiRecognizer

FIFO_PATH = "./record/inmp_rec"
MODEL_PATH = "./model/vosk-model-small-en-us-0.15"
SAMPLE_RATE = 16000
#SERVER_HOST = "192.168.56.153"
SERVER_PORT = 5000

fifo = None
sock = None
running = True

# 允許傳送的關鍵字
ALLOWED_COMMANDS = {"help", "on", "off", "of", "open","close","light","hello","bye bye","bye","help me","help help","hi"}

def signal_handler(sig, frame):
    global running
    print('\n🛑 收到 Ctrl+C，正在結束...')
    running = False
    if fifo:
        fifo.close()
    if sock:
        sock.close()
    sys.exit(0)

def reset_fifo():
    global fifo
    try:
        fifo.close()
    except:
        pass
    fifo_fd = os.open(FIFO_PATH, os.O_RDONLY | os.O_NONBLOCK)
    fifo = os.fdopen(fifo_fd, 'rb')

def recognize_and_send():
    global fifo, sock, running

    if not os.path.exists(FIFO_PATH):
        print(f"FIFO {FIFO_PATH} 不存在，正在建立...")
        try:
            os.mkfifo(FIFO_PATH)
        except OSError as e:
            print(f"❌ 建立 FIFO 失敗: {e}")
            return

    if not os.path.exists(MODEL_PATH):
        print(f"模型路徑 {MODEL_PATH} 不存在，請先下載並解壓縮")
        return

    model = Model(MODEL_PATH)
    recognizer = KaldiRecognizer(model, SAMPLE_RATE)

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((SERVER_HOST, SERVER_PORT))
    except Exception as e:
        print(f"❌ 無法連線至伺服器: {e}")
        return

    fifo_fd = os.open(FIFO_PATH, os.O_RDONLY | os.O_NONBLOCK)
    fifo = os.fdopen(fifo_fd, 'rb')

    print("✅ Vosk 語音辨識已啟動，等待音訊輸入...")

    while running:
        try:
            data = fifo.read(4096)
            if not data:
                time.sleep(0.05)
                continue

            if b"__STOP__" in data:
                print("🛑 收到 __STOP__，重置 FIFO")
                reset_fifo()
                continue

            if recognizer.AcceptWaveform(data):
                result = json.loads(recognizer.Result())
                final_text = result.get("text", "").strip().lower()

                if final_text:
                    print(f"✅ 辨識結果: '{final_text}'")

                    if final_text in ALLOWED_COMMANDS:
                        print(f"📤 符合指令，傳送到 Server: '{final_text}'")
                        try:
                            sock.sendall(final_text.encode("utf-8"))
                        except BrokenPipeError:
                            print("⚠️ Server socket 中斷，停止傳送")
                            break
                    else:
                        print(f"🚫 忽略非指令: '{final_text}'")
            else:
                # 可以選擇是否要顯示 partial（暫時先不顯示）
                pass

        except OSError as e:
            if e.errno != errno.EAGAIN:
                print("❌ FIFO 讀取錯誤:", e)
                break
        except Exception as e:
            print("❌ 語音辨識錯誤:", e)
            break

    # 清理資源
    try:
        if fifo: fifo.close()
        if sock: sock.close()
    except:
        pass

def ensure_single_instance():
    lock_file = open("/tmp/vosk_listener.lock", "w")
    try:
        fcntl.lockf(lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except IOError:
        print("🛑 vosk_listener 已在執行中，禁止重複啟動")
        sys.exit(1)

def get_wlan_ip():
    try:
        iface = "wlan0"
        addrs = netifaces.ifaddresses(iface)
        ip_info = addrs.get(netifaces.AF_INET)
        if ip_info and len(ip_info) > 0:
            return ip_info[0]['addr']
    except Exception as e:
        print("⚠️ 無法取得 wlan0 IP:", e)
    return None

def main():
    global SERVER_HOST
    ensure_single_instance()
    signal.signal(signal.SIGINT, signal_handler)

    wlan_ip = get_wlan_ip()
    if wlan_ip:
        SERVER_HOST = wlan_ip
        print(f"🌐 自動取得 wlan0 IP: {SERVER_HOST}")
    else:
        print("❌ 無法取得 wlan0 IP，請確認已連接 Wi-Fi")

    try:
        recognize_and_send()
    except Exception as e:
        print("❌ 發生未預期錯誤:", e)

if __name__ == "__main__":
    main()
