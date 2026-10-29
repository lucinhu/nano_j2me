# The security types a MIDlet names, and the `instanceof` they answer

JSR 75 declares four types around a secure connection and this runtime had none
of them:

| type | role |
| --- | --- |
| `javax.microedition.io.HttpsConnection` | an `https://` connection, `extends HttpConnection` |
| `javax.microedition.io.SecurityInfo` | the negotiated protocol, cipher suite and certificate |
| `javax.microedition.io.SecureConnection` | an `ssl://` socket connection |
| `javax.microedition.pki.Certificate` | the certificate `SecurityInfo` returns |

Their absence is not a missing feature, it is a linkage error in the middle of
working code. Two collected titles say so:

* 急速公路狂飙－新年版_84 runs `c instanceof javax.microedition.io.HttpsConnection`
  on the connection it just opened, for every request. `instanceof` resolves the
  class **before** it can answer, so a plain `http://` request that should
  simply have proceeded died with `NoClassDefFoundError: javax/microedition/io/HttpsConnection`.
* book7's checkers sample names `SecureConnection`, `SecurityInfo` and
  `pki.Certificate` in `verifyCertificate()` and casts its `ssl://` connection
  to `SecureConnection`.

## Why there are two implementation classes

This runtime cannot give a built-in class an interface list.  `implements_interface()`
in `src/core/nj_class.c` spells is-a as the super chain, and a class carries one
super, so the obvious shortcut - one HTTP implementation whose chain runs
through `HttpsConnection` - would make

    httpConnection instanceof HttpsConnection   ->  true

for every plain `http://` connection.  For 急速公路狂飙 that is worse than the
bug being fixed: the title branches on exactly this test.

So `Connector.open()` now builds one of two objects:

    http://   HttpImpl   ->  HttpConnection   (is not an HttpsConnection)
    https://  HttpsImpl  ->  HttpsConnection -> HttpConnection

and both classes declare the same state fields and bind the same natives.
`hfld()` picks the declaring class from the object it is handed, because a
field index is relative to its declaring class's layout.

## What `getSecurityInfo()` answers

`IOException`.  JSR 75 defines that answer as "the connection is closed or the
security information is not available", and it is not available here: the port's
`http_request` hook performs a whole request host-side (curl, `HttpURLConnection`)
and never hands back the TLS session.  An object full of invented protocol
names and a null certificate would be worse than the exception, and a title that
only casts to `HttpsConnection` never calls it.

`Connector.open("ssl://...")` is still refused with
`ConnectionNotFoundException`: a type existing is not a promise that TLS is
available.  `SecureConnection` exists so names resolve; no object implements it.

## Tooling note

The first version of the change spelled the shared method and native tables as
macros to avoid duplicating ~70 rows.  `tools/api_audit.py` and the two argument
gates read those tables as **text**, so the audit stopped seeing the whole
`HttpConnection` surface and reported 21 members missing.  The tables are
written out per class (the twelve-line macro was not worth an audit that lies),
and the count is back to "every one of the 982 reference members resolves".

## Verification

`tests/test_https.c` (C level): the four types exist; a `http://` object is
assignable to `HttpConnection`/`ContentConnection` and **not** to
`HttpsConnection`; an `https://` object is both; the two objects keep separate
request methods and get their own default port (80 / 443); both answer
`getResponseCode()` 200 through a fake port hook; `getSecurityInfo()` raises
`IOException`; `ssl://` is refused by name.

`tools/check_https_types.sh` (Java level, ctest `check_https_types`) builds
`tests/httpsdemo/HttpsDemo.java` against the stub API and asserts the line the
interpreter produces:

    HTTPS http_is_https=false https_is_https=true https_is_http=true url_ok=true secinfo=IOException types=true

A/B against the pre-change binary - same fixture, same command:

    pre   thrown: java/lang/NoClassDefFoundError ... javax/microedition/io/HttpsConnection
          HTTPS ERROR java.lang.NoClassDefFoundError: javax/microedition/io/HttpsConnection
    post  ok: HTTPS http_is_https=false https_is_https=true https_is_http=true url_ok=true secinfo=IOException types=true

Full `ctest` 127/127.  `tools/api_audit.py`: all 982 reference members resolve.

## Limits

The stub generator gained the four types (with the `throws java.io.IOException`
on `getSecurityInfo()`, so a fixture that catches it still compiles).  No TLS
session details are exposed yet; a port that wants to answer `getSecurityInfo()`
for real needs a hook that returns protocol, cipher suite and certificate - the
type surface is ready for it but the C side returns none.
