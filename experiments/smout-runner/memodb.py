import base64
import collections.abc as abc
import io
import urllib.parse

import aiohttp
import cbor2


__all__ = ["Multibase", "Name", "CID", "Head", "Call", "Link", "Store"]


class Multibase:
    prefixes = {}

    def __init__(self, prefix, base64_name, pad, lower):
        Multibase.prefixes[prefix] = self
        self.prefix = prefix
        self._base_decode = getattr(base64, base64_name + "decode")
        self._base_encode = getattr(base64, base64_name + "encode")
        self.pad = pad
        self.lower = lower

    @staticmethod
    def decode(text):
        prefix = text[0]
        if prefix not in Multibase.prefixes:
            raise NotImplementedError("unsupported multibase {prefix!r}")
        base = Multibase.prefixes[prefix]
        return base.decode_without_prefix(text[1:])

    def decode_without_prefix(self, text):
        if self.lower:
            text = text.upper()
        if self.pad and len(text) % self.pad:
            text += "=" * (self.pad - len(text) % self.pad)
        return self._base_decode(text)

    def encode(self, data):
        text = self._base_encode(data).decode("ascii")
        if self.pad:
            text = text.rstrip("=")
        if self.lower:
            text = text.lower()
        return self.prefix + text


Multibase.base16 = Multibase("f", "b16", None, True)
Multibase.base16upper = Multibase("F", "b16", None, False)
Multibase.base32 = Multibase("b", "b32", 8, True)
Multibase.base32upper = Multibase("B", "b32", 8, False)
Multibase.base64 = Multibase("m", "standard_b64", 4, False)
Multibase.base64pad = Multibase("M", "standard_b64", None, False)
Multibase.base64url = Multibase("u", "urlsafe_b64", 4, False)
Multibase.base64urlpad = Multibase("U", "urlsafe_b64", None, False)


class Name:
    @staticmethod
    def parse_url(url):
        orig_url = url
        if url.endswith("/"):
            raise ValueError(f"invalid name {orig_url!r}")
        url = urllib.parse.urlparse(url)
        if url.path.startswith("/cid/"):
            return CID(url.path[5:])
        elif url.path.startswith("/head/"):
            return Head(urllib.parse.unquote(url.path[6:]))
        elif url.path.startswith("/call/"):
            url = url.path[6:].split("/")
            if len(url) != 2:
                raise ValueError(f"invalid name {orig_url!r}")
            func = urllib.parse.unquote(url[0])
            args = [CID(urllib.parse.unquote(arg)) for arg in url[1].split(",")]
            return Call(func, args)
        else:
            raise ValueError(f"unknown name {orig_url!r}")


class CID(Name):
    def __init__(self, str_or_bytes):
        if isinstance(str_or_bytes, bytes):
            self.raw = str_or_bytes.lstrip(b"\x00")
        else:
            self.raw = Multibase.decode(str_or_bytes)

    def as_base64url(self):
        return Multibase.base64url.encode(self.raw)

    def as_bytes(self):
        return self.raw

    def __str__(self):
        return f"/cid/{self.as_base64url()}"

    def __repr__(self):
        return f"CID({self.as_base64url()!r})"

    def encode_cbor(self, encoder):
        encoder.encode(cbor2.CBORTag(42, b"\x00" + self.raw))


class Head(Name):
    def __init__(self, name):
        self.name = name

    def __str__(self):
        return f"/head/{urllib.parse.quote(self.name)}"

    def __repr__(self):
        return f"Head({self.name!r})"


class Call(Name):
    def __init__(self, func, args):
        self.func = func
        self.args = args

    def __str__(self):
        func = urllib.parse.quote(self.func, "")
        args = ",".join(arg.as_base64url() for arg in self.args)
        return f"/call/{func}/{urllib.parse.quote(args,'')}"

    def __repr__(self):
        return f"Call({self.func!r},{self.args!r})"


class Link:
    def __init__(self, store, *, node=None, cid=None):
        self.store = store
        self.node = node
        self.cid = cid
        if self.node is None and self.cid is None:
            raise ValueError("Neither node= or cid= provided")

    async def as_cid(self):
        if self.cid is None:
            self.cid = await self.store.add(self.node)
        return self.cid

    async def as_node(self):
        if self.node is None:
            self.node = await self.store.get(self.cid)
        return self.node

    async def as_base64url(self):
        return (await self.as_cid()).as_base64url()

    def __str__(self):
        return str(self.cid) if self.cid is not None else str(self.node)

    def __repr__(self):
        if self.cid is not None:
            return f"Link(cid={self.cid!r})"
        else:
            return f"Link(node={self.node!r})"


class Store:
    def __init__(self, uri):
        self.uri = uri.rstrip("/")
        self.session = aiohttp.ClientSession(headers={"accept": "application/cbor"})

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        await self.session.close()

    def _cbor_tag_hook(self, decoder, tag, shareable_index=None):
        if tag.tag != 42:
            return tag
        return Link(self, cid=CID(tag.value))

    def _cbor_encoder(self, encoder, value):
        value.encode_cbor(encoder)

    def _request(self, method, path, **kwargs):
        assert path.startswith("/")
        return self.session.request(method, f"{self.uri}{path}", **kwargs)

    async def _get_node_optional(self, path):
        async with self._request("get", path) as response:
            if response.status == 404:
                return None
            response.raise_for_status()
            return cbor2.loads(await response.read(), tag_hook=self._cbor_tag_hook)

    async def _put_or_post_node(self, method, path, node):
        stream = io.BytesIO()
        encoder = cbor2.CBOREncoder(stream)
        # We need our own encoding function so we can use await on Link.as_cid().
        async def encode(value):
            if isinstance(value, Link):
                (await value.as_cid()).encode_cbor(encoder)
            elif isinstance(value, abc.Mapping):
                encoder.encode_length(5, len(value))
                for key, val in value.items():
                    await encode(key)
                    await encode(val)
            elif isinstance(value, abc.ByteString):  # must check before Sequence
                encoder.encode(value)
            elif isinstance(value, str):  # must check before Sequence
                encoder.encode_string(value)
            elif isinstance(value, abc.Sequence):
                encoder.encode_length(4, len(value))
                for item in value:
                    await encode(item)
            elif isinstance(value, int):
                encoder.encode(value)
            elif isinstance(value, bool):
                encoder.encode(value)
            elif isinstance(value, float):
                encoder.encode(value)
            elif value is None:
                encoder.encode(value)
            else:
                raise ValueError(f"invalid type {type(value)} in node")

        await encode(node)
        return self._request(
            method,
            path,
            data=stream.getvalue(),
            headers={"Content-Type": "application/cbor"},
        )

    async def get_optional(self, name):
        return await self._get_node_optional(f"{name}")

    async def get(self, name):
        result = await self.get_optional(name)
        if result is None:
            raise FileNotFoundError(f"missing {name}")
        return result

    async def add(self, node):
        async with await self._put_or_post_node("post", "/cid", node) as response:
            response.raise_for_status()
            return Name.parse_url(response.headers["Location"])

    async def set(self, name, *, node=None, cid=None):
        link = Link(self, node=node, cid=cid)
        async with await self._put_or_post_node("put", str(name), link) as response:
            if response.status >= 400:
                print(await response.text())
            response.raise_for_status()
