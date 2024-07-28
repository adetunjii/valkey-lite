# valley-lite
Valkey-lite is a minimalistic C client library for the Valkey database.

It is minimalistic because it adds minimal support for the protocol, and at the same time uses a high-level printf-like API to make it much higher level than otherwise suggested by its minimal code base and the lack of explicit bindings for every Valkey command.

Apart from supporting sending commands and receiving replies, it comes with a reply parser that is decoupled from the I/O layer. It is a stream parser designed for easy reusability, which can for instance be used in higher-level language bindings for efficient reply parsing.

Hiredis only supports the binary-safe [Redis protocol](https://redis.io/docs/latest/develop/reference/protocol-spec/) so that you can use it with any Redis version >= 1.2.0.

The library comes with multiple APIs. There is the synchronous API, the asynchronous API, and the reply parsing API.
