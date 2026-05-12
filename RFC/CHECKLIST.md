# Sending the RFC

This file is the runbook for actually mailing the patch series. Run
these steps on a machine where you have:

- git
- git send-email (Debian/Ubuntu: `sudo apt-get install git-email`)
- An SMTP account you can authenticate with

The 2-patch series plus cover letter is in `RFC/outgoing/`:

- `0000-cover-letter.patch`
- `0001-RDMA-rxe-add-local-implicit-ODP-MR-support.patch`
- `0002-RDMA-rxe-advertise-IB_ODP_SUPPORT_IMPLICIT-for-local.patch`

Each runs clean through `scripts/checkpatch.pl --strict` (0 errors,
0 warnings, 0 checks).

## 1. Configure git send-email once

Edit `~/.gitconfig`:

```
[sendemail]
    smtpserver = smtp.gmail.com
    smtpserverport = 587
    smtpencryption = tls
    smtpuser = liibaaneagle@gmail.com
    confirm = always
    suppresscc = self
    chainreplyto = false
```

For Gmail, generate an app password at https://myaccount.google.com/apppasswords
and use it for `smtppass` (or let send-email prompt every time, which
is safer).

## 2. Confirm the maintainers list

From the kernel tree (`rdma/for-next` checkout):

```
./scripts/get_maintainer.pl --nogit --nogit-fallback RFC/outgoing/*.patch
```

Expected (as of this preparation):

- Zhu Yanjun <zyjzyj2000@gmail.com> (SOFT-ROCE / rxe maintainer)
- Jason Gunthorpe <jgg@ziepe.ca> (InfiniBand subsystem maintainer)
- Leon Romanovsky <leon@kernel.org> (InfiniBand subsystem maintainer)
- linux-rdma@vger.kernel.org (RXE open list)
- linux-kernel@vger.kernel.org (open list)

## 3. Re-run checkpatch right before sending

```
./scripts/checkpatch.pl --strict RFC/outgoing/*.patch
```

Should report `0 errors, 0 warnings, 0 checks`. If anything new shows
up, fix and re-export.

## 4. Send a dry run to yourself first

```
git send-email \
  --to liibaaneagle@gmail.com \
  --suppress-cc=all \
  RFC/outgoing/0000-*.patch RFC/outgoing/000[1-2]-*.patch
```

Open the result in your mail client. Check:

- Subject line is `[RFC PATCH rdma-next 0/2] ...`
- Numbered patches are `[RFC PATCH rdma-next 1/2] ...` and `2/2`
- Patches are inline plain text (not attachments).
- No mangled whitespace.
- The cover letter renders.

## 5. Send for real

```
git send-email \
  --to linux-rdma@vger.kernel.org \
  --cc zyjzyj2000@gmail.com \
  --cc jgg@ziepe.ca \
  --cc leon@kernel.org \
  --cc linux-kernel@vger.kernel.org \
  RFC/outgoing/0000-*.patch RFC/outgoing/000[1-2]-*.patch
```

git send-email will print each recipient and ask for confirmation
because of `confirm = always`. Read the recipients and type `y` for
each. Anything that looks wrong, type `n` and re-check.

## 6. Track responses

- Patchwork: https://patchwork.kernel.org/project/linux-rdma/list/
  Search for the cover-letter subject.
- lore archive: https://lore.kernel.org/linux-rdma/
- Subscribe to linux-rdma so replies arrive in your inbox:
  echo 'subscribe linux-rdma' | mail majordomo@vger.kernel.org

## 7. Replying to feedback

- Reply inline, plain text, top-quoting trimmed.
- For a v2: rebase on current for-next, address feedback, run
  checkpatch again, regenerate patches with `--subject-prefix="RFC PATCH
  v2 rdma-next"`, send.
- Use `--in-reply-to` with the Message-Id of your v1 cover letter so
  Patchwork links the versions.

## 8. Pitfalls to avoid

- Do not send patches as attachments. Inline only.
- Do not use webmail (Gmail web). It mangles whitespace and breaks
  patches. Use git send-email.
- Do not force-push the kernel branch after sending v1 without sending
  a v2 announcement. Reviewers look at the patches you sent, not your
  GitHub branch.
- Do not announce hardware comparisons or performance claims you
  cannot reproduce from this tree alone.
- Do not respond defensively to maintainer feedback. They are doing
  you a favor by reading.
