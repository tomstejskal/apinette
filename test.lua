abra = api {
  proto = http,
  host = 'localhost:8081',
  path = '/data',
  auth = basic { user = 'Supervisor', password = '' },
  verbose = false
}

result = send {
  abra.get "/firms?select=id,code,name&where=code+eq+'foo'",
  abra.post { path = "/firms?select=id,code,name", body = '{"code":"foo","name":"Foo"}' }, 
  abra.put { path = "/firms/2800000101?select=id,code,name", body = '{"name":"Bar"}' },
  abra.delete "/firms/3800000101"
}

for i = 1, #result do 
  local resp = result[i]
  if resp.err then
    print(resp.err)
  else
    print(i .. ' - status: ' .. resp.status)
    print(resp.body)
    print()
  end
end

