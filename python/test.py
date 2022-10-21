import pycam
import cv2

cam = pycam.open("rtsp://192.168.1.4:8554/")

pause = False

while True:
    try:
        image = cam.read()
    except RuntimeError as e:
        print(f"Error: {e}")
        break

    image = cv2.pyrDown(image)
    cv2.imshow("rtspcam", image)

    if pause:
        key = cv2.waitKey()
    else:
        key = cv2.pollKey()

    if key == ord("q"):
        break
    elif key == ord(" "):
        pause = not pause
