# Send RFC

Patches in `RFC/outgoing/`. checkpatch --strict: 0/0/0 on both.

## subscribe
mail `linux-rdma+subscribe@vger.kernel.org` (subject/body blank, no confirm).

## ~/.gitconfig
```
[sendemail]
    smtpserver = smtp.gmail.com
    smtpserverport = 587
    smtpencryption = tls
    smtpuser = liibaegal@gmail.com
    from = Liibaan Egal <liibaegal@gmail.com>
    confirm = always
    suppresscc = self
    chainreplyto = false
```
Gmail app password: https://myaccount.google.com/apppasswords. Let send-email prompt; do not store in .gitconfig.

## dry run
```
git send-email --to liibaegal@gmail.com --suppress-cc=all RFC/outgoing/*.patch
```
Check inbox: inline, unmangled, subjects `[RFC PATCH rdma-next 0/2]` etc.

## send
```
git send-email \
  --to linux-rdma@vger.kernel.org \
  --cc zyjzyj2000@gmail.com \
  --cc jgg@ziepe.ca \
  --cc leon@kernel.org \
  --cc linux-kernel@vger.kernel.org \
  RFC/outgoing/*.patch
```
Type `y` per recipient. Save the cover-letter Message-Id.

## URL
```
MSGID=...  # from send output, no < >
until curl -fsI "https://lore.kernel.org/linux-rdma/${MSGID}/" >/dev/null; do
  sleep 60
done
echo "https://lore.kernel.org/linux-rdma/${MSGID}/"
```
Or search lore: https://lore.kernel.org/linux-rdma/?q=add+local+implicit+ODP

## v2
```
git send-email --in-reply-to=<v1-cover-msgid> --subject-prefix="RFC PATCH v2 rdma-next" RFC/outgoing/*.patch
```

## don't
- attachments (inline only)
- Gmail web for patches (mangles whitespace)
- force-push branch after v1 send without v2
