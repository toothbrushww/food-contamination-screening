# ============================================================
# JETSON NANO - LOCAL VEGETABLE OBJECT DETECTION
# Camera + YOLOv4-Tiny + USB Serial to Teensy
#
# Required local model files:
#   ~/food_scanner/models/yolov4-tiny.cfg
#   ~/food_scanner/models/yolov4-tiny.weights
#   ~/food_scanner/models/coco.names
#
# USB camera:
#   /dev/video0
#
# Teensy serial port (auto-detected):
#   /dev/ttyACM0 or /dev/ttyUSB0
#
# Serial protocol
#   Teensy -> Jetson: OBJECT_DETECTED, SCAN_START, SCAN_DONE, SYSTEM_READY
#   Jetson -> Teensy: START_SCAN, RESULT:VEGETABLE:<name>:<confidence>,
#                      RESULT:NON_VEGETABLE
# ============================================================

import cv2
import serial
import time
import glob
from pathlib import Path
from datetime import datetime

# ============================================================
# PATHS
# ============================================================

BASE_DIR = Path.home() / "food_scanner"
MODEL_DIR = BASE_DIR / "models"
IMAGE_DIR = BASE_DIR / "captures"

CFG_FILE = MODEL_DIR / "yolov4-tiny.cfg"
WEIGHTS_FILE = MODEL_DIR / "yolov4-tiny.weights"
NAMES_FILE = MODEL_DIR / "coco.names"

# ============================================================
# SETTINGS
# ============================================================

SERIAL_BAUD = 115200
CAMERA_INDEX = 0

CAPTURE_WIDTH = 640
CAPTURE_HEIGHT = 480

INPUT_WIDTH = 416
INPUT_HEIGHT = 416

CONFIDENCE_THRESHOLD = 0.30
NMS_THRESHOLD = 0.40

NUMBER_OF_FRAMES = 3
FRAME_GAP = 0.10
SCAN_CAPTURE_DELAY = 0.20

# ============================================================
# VEGETABLE CLASSES
# ============================================================
# COCO provides: broccoli, carrot.
# Add names here if a custom-trained model includes them.

VEGETABLE_CLASSES = {
    "broccoli", "carrot", "cucumber", "tomato", "potato", "onion",
    "capsicum", "bell pepper", "pepper", "spinach", "cabbage",
    "cauliflower", "peas", "pea", "eggplant", "brinjal", "radish",
    "beetroot", "beet", "corn", "sweet corn", "pumpkin", "zucchini",
    "okra", "ladyfinger", "lettuce", "garlic", "ginger"
}

# ============================================================
# DIRECTORY SETUP
# ============================================================

MODEL_DIR.mkdir(parents=True, exist_ok=True)
IMAGE_DIR.mkdir(parents=True, exist_ok=True)

# ============================================================
# FIND TEENSY
# ============================================================

def find_teensy_port():
    candidates = []
    candidates.extend(glob.glob("/dev/ttyACM*"))
    candidates.extend(glob.glob("/dev/ttyUSB*"))

    if not candidates:
        raise RuntimeError("No Teensy serial port found.")

    candidates.sort()
    return candidates[0]


# ============================================================
# LOAD CLASS NAMES
# ============================================================

def load_class_names():
    if not NAMES_FILE.exists():
        raise FileNotFoundError(f"Missing class file: {NAMES_FILE}")

    with open(NAMES_FILE, "r", encoding="utf-8") as file:
        names = [line.strip() for line in file if line.strip()]

    return names


# ============================================================
# LOAD YOLO MODEL
# ============================================================

def load_model():
    if not CFG_FILE.exists():
        raise FileNotFoundError(f"Missing config file: {CFG_FILE}")
    if not WEIGHTS_FILE.exists():
        raise FileNotFoundError(f"Missing weights file: {WEIGHTS_FILE}")

    print("Loading YOLO model...")
    net = cv2.dnn.readNet(str(WEIGHTS_FILE), str(CFG_FILE))

    # Try Jetson CUDA acceleration, fall back to CPU.
    try:
        net.setPreferableBackend(cv2.dnn.DNN_BACKEND_CUDA)
        net.setPreferableTarget(cv2.dnn.DNN_TARGET_CUDA_FP16)
        print("OpenCV DNN backend: CUDA FP16")
    except Exception as error:
        print("CUDA DNN unavailable, falling back to CPU.")
        print(error)
        net.setPreferableBackend(cv2.dnn.DNN_BACKEND_OPENCV)
        net.setPreferableTarget(cv2.dnn.DNN_TARGET_CPU)

    return net


# ============================================================
# CAMERA
# ============================================================

def open_camera():
    print("Opening camera...")
    camera = cv2.VideoCapture(CAMERA_INDEX)

    if not camera.isOpened():
        raise RuntimeError(f"Could not open camera {CAMERA_INDEX}.")

    camera.set(cv2.CAP_PROP_FRAME_WIDTH, CAPTURE_WIDTH)
    camera.set(cv2.CAP_PROP_FRAME_HEIGHT, CAPTURE_HEIGHT)
    camera.set(cv2.CAP_PROP_FPS, 30)
    time.sleep(1.0)

    return camera


# ============================================================
# YOLO DETECTION
# ============================================================

def detect_objects(frame, net, class_names):
    height, width = frame.shape[:2]

    blob = cv2.dnn.blobFromImage(
        frame, 1 / 255.0, (INPUT_WIDTH, INPUT_HEIGHT),
        swapRB=True, crop=False
    )

    net.setInput(blob)
    output_layers = net.getUnconnectedOutLayersNames()
    outputs = net.forward(output_layers)

    boxes, confidences, class_ids = [], [], []

    for output in outputs:
        for detection in output:
            if len(detection) < 6:
                continue

            scores = detection[5:]
            class_id = int(scores.argmax())
            class_score = float(scores[class_id])
            objectness = float(detection[4])
            confidence = class_score * objectness

            if confidence < CONFIDENCE_THRESHOLD:
                continue

            center_x = int(detection[0] * width)
            center_y = int(detection[1] * height)
            box_width = int(detection[2] * width)
            box_height = int(detection[3] * height)
            x = int(center_x - box_width / 2)
            y = int(center_y - box_height / 2)

            boxes.append([x, y, box_width, box_height])
            confidences.append(confidence)
            class_ids.append(class_id)

    if not boxes:
        return []

    indices = cv2.dnn.NMSBoxes(
        boxes, confidences, CONFIDENCE_THRESHOLD, NMS_THRESHOLD
    )

    detections = []
    if len(indices) == 0:
        return detections

    for index in indices:
        index = int(index[0]) if hasattr(index, "__len__") else int(index)
        class_id = class_ids[index]

        if class_id < 0 or class_id >= len(class_names):
            continue

        name = class_names[class_id].lower().strip()
        confidence = confidences[index]
        x, y, w, h = boxes[index]

        detections.append({
            "name": name,
            "confidence": confidence,
            "box": (x, y, w, h),
            "class_id": class_id
        })

    return detections


# ============================================================
# BEST VEGETABLE DETECTION
# ============================================================

def get_best_vegetable(detections):
    candidates = [
        d for d in detections
        if d["name"].lower().strip() in VEGETABLE_CLASSES
    ]

    if not candidates:
        return None

    candidates.sort(key=lambda item: item["confidence"], reverse=True)
    return candidates[0]


# ============================================================
# DRAW DETECTIONS
# ============================================================

def draw_detections(frame, detections):
    for detection in detections:
        name = detection["name"]
        confidence = detection["confidence"]
        x, y, w, h = detection["box"]

        cv2.rectangle(frame, (x, y), (x + w, y + h), (255, 255, 255), 2)
        label = f"{name} {confidence * 100:.1f}%"
        cv2.putText(
            frame, label, (max(0, x), max(20, y - 8)),
            cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 2
        )

    return frame


# ============================================================
# CAMERA SCAN
# ============================================================

def scan_camera(camera, net, class_names):
    print("Preparing camera...")
    time.sleep(SCAN_CAPTURE_DELAY)

    best_detection = None
    best_frame = None

    for frame_number in range(NUMBER_OF_FRAMES):
        ok, frame = camera.read()

        if not ok:
            print("Camera frame failed.")
            time.sleep(FRAME_GAP)
            continue

        detections = detect_objects(frame, net, class_names)
        vegetable = get_best_vegetable(detections)
        frame = draw_detections(frame, detections)

        if vegetable is not None:
            if (best_detection is None
                    or vegetable["confidence"] > best_detection["confidence"]):
                best_detection = vegetable
                best_frame = frame.copy()
        elif best_frame is None:
            best_frame = frame.copy()

        print(f"Frame {frame_number + 1}: {len(detections)} detections")
        time.sleep(FRAME_GAP)

    # Save the representative frame for this cycle.
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    image_path = IMAGE_DIR / f"scan_{timestamp}.jpg"

    if best_frame is not None:
        cv2.imwrite(str(image_path), best_frame)
        print(f"Image saved: {image_path}")

    if best_detection is not None:
        name = best_detection["name"]
        confidence_percent = int(round(best_detection["confidence"] * 100))

        print()
        print("VEGETABLE DETECTED")
        print(f"Name: {name}")
        print(f"Confidence: {confidence_percent}%")

        return True, name, confidence_percent

    print()
    print("NO VEGETABLE DETECTED")
    return False, "UNKNOWN", 0


# ============================================================
# SEND RESULT TO TEENSY
# ============================================================

def send_result(teensy, is_vegetable, name, confidence):
    if is_vegetable:
        message = f"RESULT:VEGETABLE:{name}:{confidence}"
    else:
        message = "RESULT:NON_VEGETABLE"

    teensy.write((message + "\n").encode("utf-8"))
    teensy.flush()

    print("JETSON -> TEENSY:")
    print(message)


# ============================================================
# MAIN
# ============================================================

def main():
    print()
    print("========================================")
    print("JETSON NANO FOOD SCANNER")
    print("LOCAL VEGETABLE OBJECT DETECTION")
    print("========================================")
    print()

    class_names = load_class_names()
    net = load_model()
    print(f"Classes loaded: {len(class_names)}")

    camera = open_camera()

    teensy_port = find_teensy_port()
    print(f"Connecting to Teensy: {teensy_port}")

    teensy = serial.Serial(teensy_port, SERIAL_BAUD, timeout=0.1)
    time.sleep(2)
    teensy.reset_input_buffer()
    teensy.reset_output_buffer()

    print()
    print("JETSON READY")
    print("Waiting for Teensy...")
    print()

    pending_result = None

    try:
        while True:
            line = teensy.readline()
            if not line:
                continue

            command = line.decode("utf-8", errors="ignore").strip()
            if not command:
                continue

            print("TEENSY -> JETSON:", command)

            if command == "OBJECT_DETECTED":
                print("Object detected.")
                teensy.write(b"START_SCAN\n")
                teensy.flush()
                print("JETSON -> TEENSY: START_SCAN")

            elif command == "SCAN_START":
                print()
                print("SCAN STARTED")
                pending_result = scan_camera(camera, net, class_names)
                print("Camera/ML scan complete.")

            elif command == "SCAN_DONE":
                print("Teensy optical scan done.")
                if pending_result is None:
                    pending_result = (False, "UNKNOWN", 0)

                is_vegetable, name, confidence = pending_result
                send_result(teensy, is_vegetable, name, confidence)
                pending_result = None

            elif command == "SYSTEM_READY":
                print("Teensy: SYSTEM READY")

    except KeyboardInterrupt:
        print()
        print("Stopping Jetson scanner...")

    finally:
        camera.release()
        teensy.close()
        print("Jetson scanner stopped.")


# ============================================================
# ENTRY POINT
# ============================================================

if __name__ == "__main__":
    main()
