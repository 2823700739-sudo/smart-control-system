"""
手势识别 — 离线数据增强 + 迁移学习训练
用法: python train_model.py
"""
import os, sys, random, warnings
import numpy as np
import torch, torch.nn as nn, torch.optim as optim
from torch.utils.data import DataLoader, Dataset
from torchvision import datasets, transforms, models
from sklearn.model_selection import StratifiedShuffleSplit
from PIL import Image, ImageFilter, ImageEnhance
from collections import Counter

warnings.filterwarnings("ignore")

DATASET_DIR = r"D:\esp32\image"
AUG_DIR     = r"D:\esp32\image_augmented"
OUTPUT_DIR  = r"D:\esp32\5-26item\model_trained"
IMG_SIZE    = 96
AUG_PER_IMG = 8
BATCH       = 32
EPOCHS      = 50
LR          = 0.0005

os.makedirs(OUTPUT_DIR, exist_ok=True)


print("=" * 55)
print("Step 1: 离线数据增强 (每张图生成 %d 个变体)" % AUG_PER_IMG)

if not os.path.exists(AUG_DIR):
    os.makedirs(AUG_DIR)

    for cls_name in os.listdir(DATASET_DIR):
        cls_src = os.path.join(DATASET_DIR, cls_name)
        cls_dst = os.path.join(AUG_DIR, cls_name)
        if not os.path.isdir(cls_src):
            continue
        os.makedirs(cls_dst, exist_ok=True)

        files = [f for f in os.listdir(cls_src) if f.lower().endswith(('.jpg', '.jpeg', '.png', '.bmp'))]
        for fn in files:
            img = Image.open(os.path.join(cls_src, fn)).convert("RGB")
            basename = os.path.splitext(fn)[0]

            # 原图
            img.save(os.path.join(cls_dst, basename + "_orig.jpg"), quality=92)

            for k in range(AUG_PER_IMG):
                aug = img.copy()
                r = random.random()

                if r < 0.25:       # 水平翻转
                    aug = aug.transpose(Image.FLIP_LEFT_RIGHT)
                elif r < 0.40:     # 旋转 ±20°
                    aug = aug.rotate(random.uniform(-20, 20), expand=False)
                elif r < 0.55:     # 亮度 ±30%
                    fac = random.uniform(0.7, 1.3)
                    aug = ImageEnhance.Brightness(aug).enhance(fac)
                elif r < 0.70:     # 对比度 ±30%
                    fac = random.uniform(0.7, 1.3)
                    aug = ImageEnhance.Contrast(aug).enhance(fac)
                elif r < 0.82:     # 轻微模糊（模拟 OV2640 画质）
                    aug = aug.filter(ImageFilter.GaussianBlur(radius=random.uniform(0.3, 1.0)))
                elif r < 0.92:     # 轻微噪声
                    arr = np.array(aug, dtype=np.int16)
                    noise = np.random.randint(-12, 12, arr.shape, dtype=np.int16)
                    arr = np.clip(arr + noise, 0, 255).astype(np.uint8)
                    aug = Image.fromarray(arr)
                else:              # 饱和度 ±30%
                    fac = random.uniform(0.7, 1.3)
                    aug = ImageEnhance.Color(aug).enhance(fac)

                aug.save(os.path.join(cls_dst, f"{basename}_aug{k}.jpg"), quality=88)

        print(f"  {cls_name}: {len(files)} 张 → {len(files) * (AUG_PER_IMG + 1)} 张")

    print("增强完成，数据集: %s" % AUG_DIR)
else:
    print("增强目录已存在，跳过 → %s" % AUG_DIR)

# ==============================================
# Step 2: 加载数据集
# ==============================================
print("\n" + "=" * 55)
print("Step 2: 加载数据集")

train_tf = transforms.Compose([
    transforms.Resize((IMG_SIZE, IMG_SIZE)),
    transforms.RandomHorizontalFlip(p=0.5),
    transforms.RandomRotation(15),
    transforms.RandomAffine(0, translate=(0.08, 0.08), scale=(0.9, 1.1)),
    transforms.ColorJitter(brightness=0.25, contrast=0.25, saturation=0.2),
    transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
])

val_tf = transforms.Compose([
    transforms.Resize((IMG_SIZE, IMG_SIZE)),
    transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
])

full_ds = datasets.ImageFolder(AUG_DIR)
class_names = full_ds.classes
targets = np.array(full_ds.targets)
n_total = len(full_ds)

print(f"类别: {class_names}")
print(f"总样本: {n_total}  分布: {dict(Counter(full_ds.targets))}")

splitter = StratifiedShuffleSplit(n_splits=1, test_size=0.2, random_state=42)
train_idx, val_idx = next(splitter.split(np.zeros(n_total), targets))
print(f"训练: {len(train_idx)}  验证: {len(val_idx)}")

class Wrapper(Dataset):
    def __init__(self, ds, idx, tf):
        self.ds, self.idx, self.tf = ds, idx, tf
    def __len__(self):
        return len(self.idx)
    def __getitem__(self, i):
        img, lab = self.ds[self.idx[i]]
        return self.tf(img), lab

train_wrap = Wrapper(full_ds, train_idx, train_tf)
val_wrap   = Wrapper(full_ds, val_idx,   val_tf)

train_ld = DataLoader(train_wrap, batch_size=BATCH, shuffle=True,
                      num_workers=0, pin_memory=True)
val_ld   = DataLoader(val_wrap, batch_size=BATCH, shuffle=False,
                      num_workers=0, pin_memory=True)

n_train = len(train_idx)
n_val   = len(val_idx)

# ==============================================
# Step 3: 模型 & 训练
# ==============================================
print("\n" + "=" * 55)
print("Step 3: 训练 MobileNetV3-Small (迁移学习)")

device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
print(f"设备: {device}")

model = models.mobilenet_v3_small(weights=models.MobileNet_V3_Small_Weights.DEFAULT)
model.classifier[-1] = nn.Linear(model.classifier[-1].in_features, len(class_names))
model = model.to(device)

criterion = nn.CrossEntropyLoss(label_smoothing=0.1)
optimizer = optim.AdamW(model.parameters(), lr=LR, weight_decay=1e-4)
scheduler = optim.lr_scheduler.CosineAnnealingWarmRestarts(optimizer, T_0=10, T_mult=2)

best_acc, best_epoch = 0.0, 0
for epoch in range(EPOCHS):
    model.train()
    tr_loss, tr_correct = 0.0, 0
    for imgs, labels in train_ld:
        imgs, labels = imgs.to(device), labels.to(device)
        optimizer.zero_grad()
        loss = criterion(model(imgs), labels)
        loss.backward()
        optimizer.step()
        tr_loss += loss.item() * imgs.size(0)
        tr_correct += (model(imgs).argmax(1) == labels).sum().item()
    scheduler.step()

    model.eval()
    vl_loss, vl_correct = 0.0, 0
    with torch.no_grad():
        for imgs, labels in val_ld:
            imgs, labels = imgs.to(device), labels.to(device)
            out = model(imgs)
            vl_loss += criterion(out, labels).item() * imgs.size(0)
            vl_correct += (out.argmax(1) == labels).sum().item()

    tr_acc = tr_correct / n_train
    vl_acc = vl_correct / n_val

    star = " ★" if vl_acc > best_acc else ""
    print(f"Epoch {epoch+1:3d}/{EPOCHS}  "
          f"tr_loss={tr_loss/n_train:.3f}  tr_acc={tr_acc:.3f}  "
          f"vl_loss={vl_loss/n_val:.3f}  vl_acc={vl_acc:.3f}{star}")

    if vl_acc > best_acc:
        best_acc, best_epoch = vl_acc, epoch + 1
        torch.save(model.state_dict(), os.path.join(OUTPUT_DIR, "gesture_best.pth"))

print(f"\n最佳: epoch={best_epoch}  val_acc={best_acc:.3f}")

# ==============================================
# Step 4: 导出 TorchScript
# ==============================================
print("\n" + "=" * 55)
print("Step 4: 导出模型")

model.load_state_dict(torch.load(os.path.join(OUTPUT_DIR, "gesture_best.pth"),
                       map_location="cpu", weights_only=True))
model.eval()
m = model.to("cpu")

example = torch.randn(1, 3, IMG_SIZE, IMG_SIZE)
ts = torch.jit.trace(m, example)
ts.save(os.path.join(OUTPUT_DIR, "gesture_model.pt"))

with open(os.path.join(OUTPUT_DIR, "labels.txt"), "w") as f:
    f.write("\n".join(class_names))

print(f"\n已保存到 {OUTPUT_DIR}/")
print(f"  gesture_best.pth   (权重)")
print(f"  gesture_model.pt   (TorchScript)")
print(f"  labels.txt         (类别)")
print(f"\n标签: {class_names}")
print(f"推理: python pc_inference.py")
