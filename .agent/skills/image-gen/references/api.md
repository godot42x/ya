# Pollinations.AI Image API 参考（官方 APIDOCS 生图章节摘录）

## 端点

```
GET https://image.pollinations.ai/prompt/{prompt}
```

prompt 需 URL 编码（空格 → %20）。GET 直接返回图片字节。

## 参数

| 参数 | 类型 | 说明 | 默认 |
|---|---|---|---|
| prompt | string | 图片描述（必填） | - |
| model | string | 生图模型 | flux |
| width | integer | 宽（像素） | 1024 |
| height | integer | 高（像素） | 1024 |
| seed | integer | 固定种子 → 同 prompt 逐像素一致 | 随机 |
| nologo | boolean | 去水印（需注册账号） | false |
| enhance | boolean | AI 优化 prompt，更稳更慢 | false |
| private | boolean | 不出现在公开 feed | false |

## 模型列表

`GET https://image.pollinations.ai/models`

文档声明的集合（动态，可能变化）：`flux, flux-realism, flux-anime, flux-3d, any-dark, turbo`。
实测还见默认轻量 `sana`。

## Python 示例（官方）

```python
import requests
from urllib.parse import quote

prompt = "A serene mountain landscape at sunrise"
url = f"https://image.pollinations.ai/prompt/{quote(prompt)}"
params = {"width": 1280, "height": 720, "model": "flux"}
response = requests.get(url, params=params, timeout=60)
with open("mountain.jpg", "wb") as f:
    f.write(response.content)
```

## JavaScript 示例（官方）

```js
const fetch = require('node-fetch');
const fs = require('fs');
const prompt = "A futuristic city with flying cars";
const url = `https://image.pollinations.ai/prompt/${encodeURIComponent(prompt)}?width=1280&height=720&model=flux`;
fetch(url).then(r => r.buffer()).then(b => { fs.writeFileSync('city.jpg', b); });
```

## 命令行示例

```bash
curl -o sunset.jpg "https://image.pollinations.ai/prompt/beautiful%20sunset%20over%20ocean"
curl -o city.jpg "https://image.pollinations.ai/prompt/cyberpunk%20city%20at%20night?width=1920&height=1080&seed=42&model=flux"
```

## 注意事项

- 无 Key、无注册即可用；匿名请求偶发 402/429 限流，间隔重试可恢复。
- 去水印 / 提升限速需注册账号。
- 模型名随后端动态变化，以 `/models` 接口为准；Skill 脚本内置默认集合与重试。
