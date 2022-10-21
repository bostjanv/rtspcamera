with open("lena.txt") as f:
    lines = f.readlines()

lines = [x.strip() for x in lines]
lines = [x.split(" | ")[1] for x in lines]
bytes = bytes.fromhex("".join(lines))

with open("/tmp/lena.h264", "wb") as f:
    f.write(bytes)
