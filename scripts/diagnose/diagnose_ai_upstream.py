import json
import os
from urllib.error import HTTPError
from urllib.request import Request, urlopen

with open("/etc/health-platform.env", encoding="utf-8") as env_file:
    for line in env_file:
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            name, value = line.split("=", 1)
            os.environ[name] = value.strip().strip('"').strip("'")

body = {
    "model": os.getenv("AI_MODEL", "deepseek-chat"),
    "messages": [{"role": "user", "content": "reply ok"}],
    "max_tokens": 20,
}
url = os.getenv("AI_BASE_URL", "https://api.deepseek.com").rstrip("/") + "/chat/completions"
request = Request(
    url,
    data=json.dumps(body).encode("utf-8"),
    headers={
        "Authorization": "Bearer " + os.environ["AI_API_KEY"],
        "Content-Type": "application/json",
    },
    method="POST",
)
try:
    with urlopen(request, timeout=20) as response:
        result = json.loads(response.read().decode("utf-8"))
        print("upstream_http", response.status)
        print("reply_present", bool(result.get("choices")))
except HTTPError as error:
    print("upstream_http", error.code)
    print(error.read().decode("utf-8", errors="replace")[:1000])
