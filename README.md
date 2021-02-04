# Apinette

Apinette is a tool to run REST API scripts written in Lua programming language.
It could be used for example to run ad hoc API scripts or to write complicated API tests using all the power of Lua.

## Limitations

Apinette in its current state is very simple and limited.
There is for example only one supported authorization scheme now (basic auth).

## Planned features

- ~~REPL for interactive usage~~
- support for other authorization schemes
- Windows compilation
- ~~URL encoding~~

## Compilation

Apinette is using meson build system.
You can setup the build using standard meson command:

```
meson setup build
```

Then you can compile the project:

```
meson compile -C build
```

or

```
cd build
ninja
```

## Usage

```
apinette [SCRIPT]
```

If apinette runs without arguments, it will start a REPL.
Otherwise it expects one argument on the command line, which is the Lua script to run.

## Lua functions

### endpoint

Create new api endpoint. It expects a table containing these fields:
- `proto` - HTTP protocol (strings "http" or "https" or global variables http or https)
- `host` - hostname (with optional port after colon)
- `path` - basic url path (optional)
- `auth` - authorization object (optional)
- `verbose` - run in verbose mode (prints details about requests and responses)
- `handle_response' - a function, which receives each response table and returns nothing
                      (ie. to log responses, to handle error status codes)

It returns and endpoint object, which contains following functions to create API requests:
- `get` - returns GET request
- `post` - returns POST request
- `put` - returns PUT request
- `delete` - returns DELETE request
- `request` - returns a custom request, see bellow

Each endpoint function expects a string or a table as an argument.
String is an URL path, table could have following fields:
- `path` - url path
- `headers` - table containing HTTP headers (ie. { Accept = 'application/json' })
- `body` - a string a table containing the body of POST request
- `handle_response` - a function, which receives the response and returns nothing
                      (ie. to convert response body)
- `method` - a HTTP method of custom request (there are defined global variables
             GET, POST, PUT and DELETE, which contain respective HTTP method strings)

If body is table, then it is encoded as json object and HTTP header Content-Type
is set to 'application/json'. If the supplied headers also contain Content-Type header,
it will override the implicit value.

### basic_auth

It creates an auth object and expects a table containing these fields:
- `user`
- `password`

### to_json

Converts a Lua value into json string.
Conversion of table to json is implicit, you don't need to call this function
to write request body.

### from_json

Converts a json string into a Lua value.
Conversion of json to table is implicit, you don't need to call this function
to parse response body.

### send

Sends single request or multiple requests in parallel.
It expects a request object or a list of request objects.
It returns a single result table in case of single request object, or a list of result tables in case of list of requests.

Result table contains following fields:
- `status` - HTTP status
- `headers` - a table containing HTTP headers (ie. { ['Content-Type'] = 'application/json' })
- `body` - string or table containing the body of the response
- `err` - a string containing possible transport error or nil
- `method` - request method
- `url` - request URL
- `total_time` - total time of response in seconds

If response contains Content-Type header with value 'application/json', then body
is table decoded from json string.

### url_encode

URL encodes its argument.

### url_decode

URL decodes its argument.

## Example

```lua
example = endpoint {
  proto = http,
  host = 'api.example.com'
  auth = basic_auth { user = 'test', password = 'testpassword' }
}

result = send(example.get "/")
// result.body is table (created from json)
for k, v in pairs(result.body) do
  print(k .. ': ' .. v)
end

// automatically converts body as table into json
result = send {
  example.post { path = "/", body = { foo = 'bar' } },
  example.delete "/foo"
}
print(result[1].status)
print(result[2].status)
```
