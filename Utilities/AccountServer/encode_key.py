import base64
import sys

decoded = base64.b64decode(sys.argv[1])
decoded = [ord(x) for x in decoded]

hardcoded = base64.b64decode(sys.argv[2])
hardcoded = [ord(x) for x in hardcoded]

combined = [x ^ y for x, y in zip(hardcoded, decoded)]

combined = ''.join([chr(x) for x in combined])
combined = base64.b64encode(combined)

print combined
