# Apinette

Apinette is a tool to run REST API scripts written in Lua programming language.
It could be used for example to run ad hoc API scripts or to write complicated API tests using all the power of Lua.

## Limitations

Apinette in its current state is very simple and limited.
There is for example only one supported authorization scheme now (basic auth).

## Planned features

- REPL for interactive usage
- support for other authorization schemes
- Windows compilation
- UTL encoding

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
apinette SCRIPT
```

Appinete expects one argument on the command line, which is the Lua script to run.

## Lua functions

### api

Create new api endpoint. It expects a table containing these fields:
- `proto` - HTTP protocol (strings "http" or "https" or global variables http or https)
- `host` - hostname (with optional port after colon)
- `path` - basic url path (optional)
- `auth` - authorization object (optional)
- `verbose` - run in verbose mode (prints details about requests and responses)

It returns and api object, which contains following functions:

#### {api}.get

Returns new GET request. It expects a string containing url path as its argument, or a table containing following fields:
- `path` - url path
- `headers` - list of strings, each containing a HTTP header (ie. "Accept: application/json")

#### {api}.post

Returns new POST request. It expects a table containing following fields:

- `path` - see {api}.get
- `headers` - see {api}.get
- `body` - a string containing the body of POST request

#### {api}.put

Returns new PUT request. It expects a table (see {api}.post)

#### {api}.delete

Returns new DELETE request. For arguments see {api}.get.

### to_json

Converts a Lua value into json string.

### from_json

Converts a json string into a Lua value.

### send

Sends single request or multiple requests in parallel.
It expects a request object or a list of request objects.
It returns a single result table in case of single request object, or a list of result tables in case of list of requests.

Result table contains following fields:
- `status` - HTTP status
- `headers` - list of header strings
- `body` - body of the response or nil
- `err` - a string containing possible transport error or nil

### use

Loads a file containing Lua script a run it
