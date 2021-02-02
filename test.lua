ep = endpoint {
  proto = http,
  host = 'localhost:8000',
  handle_response = function (resp)
    ep_handle_response_called = true
    if resp.err then
      error(resp.err)
    elseif resp.status >= 400 then
      error('request faile with status ' .. resp.status)
    end
  end
}

resp = send(ep.get {
  path = '/1',
  handle_response = function (body)
    req_handle_response_called = true
    return body
  end
})

assert(ep_handle_response_called, 'handle_response function of endpoint hasn\'t been called')
assert(req_handle_response_called, 'handle_response function of request hasn\'t been called')
assert(not resp.err, 'unexpected error: ' .. (resp.err or ''))
assert(resp.status == 200, 'invalid response status: ' .. resp.status)
assert(type(resp.body) == 'table', 'invalid type of response body: ' .. type(resp.body))
assert(resp.body.title == "example", 'unexpected title: ' .. resp.body.title)
assert(resp.body.description == "this is an example todo item",
  'unexpected description: ' .. resp.body.description)

