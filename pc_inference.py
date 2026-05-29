"""
手势识别实时推理 + LED 控制回传
ESP32 UDP JPEG → PC 推理 → UDP 命令回传
"""
import socket, struct, numpy as np, cv2, time, os, torch

MODEL_DIR  = os.path.join(os.path.dirname(__file__), "model_trained")
UDP_PORT   = 5555
CMD_PORT   = 5556
IMG_SIZE   = 96

MEAN = np.array([0.485, 0.456, 0.406], dtype=np.float32)
STD  = np.array([0.229, 0.224, 0.225], dtype=np.float32)

# 手势标签对应 LED 控制命令  3为最亮，0为最暗
LABEL_CMD = {"fist": b'0', "one": b'1', "two": b'2', "three": b'3'}

print("加载模型...", end=" ", flush=True)
device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

model = torch.jit.load(os.path.join(MODEL_DIR, "gesture_model.pt"), map_location=device)
model.eval()

with open(os.path.join(MODEL_DIR, "labels.txt")) as f:
    labels = f.read().strip().split("\n")
print(f"OK  {labels}  {device}")

def preprocess(img_bgr):
    img = cv2.resize(img_bgr, (IMG_SIZE, IMG_SIZE))
    img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    t = torch.from_numpy(img).permute(2, 0, 1).float() / 255.0
    for c in range(3):
        t[c] = (t[c] - MEAN[c]) / STD[c]
    return t.unsqueeze(0).to(device)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 256 * 1024)
sock.bind(("0.0.0.0", UDP_PORT))
sock.settimeout(0.3)

print(f"UDP 收:{UDP_PORT}  命令发→ ESP32:{CMD_PORT}")

cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
esp32_addr = None

last_infer = 0
last_label = None
cv2.namedWindow("ESP32", cv2.WINDOW_NORMAL)

while True:
    try:
        data, addr = sock.recvfrom(65535)
    except socket.timeout:
        if cv2.waitKey(10) & 0xFF == ord('q'):
            break
        continue

    if esp32_addr is None:
        esp32_addr = (addr[0], CMD_PORT)
        print(f"ESP32 address: {addr[0]}:{CMD_PORT}")

    if len(data) < 12:
        continue

    jpg = b'\xff\xd8' + data[10:]
    arr = np.frombuffer(jpg, dtype=np.uint8)
    img = cv2.imdecode(arr, cv2.IMREAD_COLOR)
    if img is None:
        continue

    now = time.time()
    if now - last_infer > 0.5:
        last_infer = now
        t = preprocess(img)
        with torch.no_grad():
            out = model(t)
            probs = torch.softmax(out, dim=1)[0].cpu().numpy()

        top = sorted(zip(labels, probs), key=lambda x: x[1], reverse=True)
        best_label = top[0][0]

        print(f">>> {best_label:8s}  {top[0][1]:.2f}")
        for lb, p in top:
            print(f"  {lb:8s}  {p:.4f}  {'#' * int(p * 40)}")

        if best_label != last_label and best_label in LABEL_CMD:
            # 发送 LED 控制命令
            cmd_sock.sendto(LABEL_CMD[best_label], esp32_addr)
            print(f"  → LED cmd: {best_label}")
            last_label = best_label

    hh = img.shape[0] // 2
    if 'top' in dir():
        for i, (lb, p) in enumerate(top):
            color = (0, 255, 0) if p > 0.6 else (0, 200, 200)
            cv2.putText(img, f"{lb}: {p:.2f}", (10, hh + 25 * i),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, color, 2)

    cv2.imshow("ESP32", img)
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cv2.destroyAllWindows()
sock.close()
cmd_sock.close()
