"""content.py — url、api key、圖片這些雜事，全是不碰狀態的純函式。"""

import base64
import mimetypes
import os


def normalize_base_url(url: str) -> str:
    """url 可以是 base url 或完整的 /chat/completions 端點，統一轉成 OpenAI() 要的 base_url。"""
    url = url.rstrip("/")
    if url.endswith("/chat/completions"):
        url = url[: -len("/chat/completions")]
    return url.rstrip("/")


def root_url(url: str) -> str:
    """再把尾巴的 /v1 也拿掉，得到 proxy 的根位址（/model/info 這種管理端點掛在這裡）。"""
    url = normalize_base_url(url)
    if url.endswith("/v1"):
        url = url[: -len("/v1")]
    return url


def resolve_key(key):
    """key 沒給就吃環境變數 OPENAI_API_KEY，再沒有就用 "hello" 頂著（本機 proxy 不檢查）。"""
    if key is not None:
        return key
    return os.environ.get("OPENAI_API_KEY") or "hello"


def encode_image(path_or_url: str) -> dict:
    """把一張圖片（本機路徑或 http(s) URL）轉成 OpenAI 的 image_url content part。"""
    if path_or_url.startswith("http://") or path_or_url.startswith("https://"):
        url = path_or_url
    else:
        mime, _ = mimetypes.guess_type(path_or_url)
        mime = mime or "image/png"
        with open(path_or_url, "rb") as f:
            b64 = base64.b64encode(f.read()).decode("ascii")
        url = f"data:{mime};base64,{b64}"
    return {"type": "image_url", "image_url": {"url": url}}


def build_content(prompt, images):
    """沒有圖片就送純文字字串；有圖片就組成 content-parts 的 list。"""
    if not images:
        return prompt
    parts = [{"type": "text", "text": prompt or ""}]
    parts.extend(encode_image(img) for img in images)
    return parts
