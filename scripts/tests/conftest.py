"""Put `scripts/` on the import path, so a test imports `util.lock` the way a script does."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
