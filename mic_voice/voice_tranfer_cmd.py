import os
import json
from vosk import Model, KaldiRecognizer

FIFO_PATH = "/tmp/rec_fifo"
MODEL_PATH = "vosk-model-small-en-us-0.15"
SAMPLE_RATE = 16000

def recognize_from_fifo():
    if not os.path.exists(FIFO_PATH):
        print(f"FIFO {FIFO_PATH} 不存在，請先建立：mkfifo /tmp/audio_pipe")
        return

    if not os.path.exists(MODEL_PATH):
        print(f"模型路徑 {MODEL_PATH} 不存在，請先下載並解壓縮")
        return

    model = Model(MODEL_PATH)
    recognizer = KaldiRecognizer(model, SAMPLE_RATE)

    print("正在等待音訊資料輸入 FIFO...")
    with open(FIFO_PATH, "rb") as f:
        while True:
            data = f.read(4096)
            if len(data) == 0:
                continue  # 等待資料進來

            if recognizer.AcceptWaveform(data):
                result = json.loads(recognizer.Result())
                print("語音辨識結果:", result.get("text", ""))
            else:
                partial = json.loads(recognizer.PartialResult())
                print("中間辨識:", partial.get("partial", ""))


def main():
    try:
        recognize_from_fifo()
    except KeyboardInterrupt:
        print("\n 中斷語音辨識，結束程式")
    except Exception as e:
        print("發生錯誤:", e)

if __name__ == "__main__":
    main()
