use './abra.lua'

function print_firm(firm)
    print(firm.id, firm.code, firm.name)
end;

function print_firms(firms)
  for i = 1, #firms do
    print_firm(firms[i])
  end;
end

result = send(abra.get "/firms")
print_firms(from_json(result.body))

result = send(
  abra.post {
    path = "/firms",
    body = to_json({ code = "abc", name = "ABC" })
  }
)

print_firm(from_json(result.body))

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

