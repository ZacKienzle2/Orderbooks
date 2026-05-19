# Commit signing

Commits and tags should be signed. Repo-local config until a key is wired up:

```bash
git config commit.gpgsign false
git config tag.gpgsign false
```

## SSH signing

```bash
ssh-keygen -t ed25519 -C "zac.kienzle@gmail.com" -f ~/.ssh/id_ed25519_signing
gh ssh-key add ~/.ssh/id_ed25519_signing.pub --type signing --title "$(hostname)"

git config gpg.format ssh
git config user.signingkey ~/.ssh/id_ed25519_signing.pub
git config commit.gpgsign true
git config tag.gpgsign true
```

Local verification needs an allowed-signers file:

```bash
printf '%s %s\n' "$(git config user.email)" \
  "$(cat ~/.ssh/id_ed25519_signing.pub)" \
  >> ~/.config/git/allowed_signers
git config gpg.ssh.allowedSignersFile ~/.config/git/allowed_signers
```

GitHub docs: <https://docs.github.com/authentication/managing-commit-signature-verification/about-commit-signature-verification#ssh-commit-signature-verification>.

## GPG signing

```bash
gpg --list-secret-keys --keyid-format=long
gpg --armor --export <KEYID> | gh gpg-key add -

git config gpg.format openpgp
git config user.signingkey <KEYID>
git config commit.gpgsign true
git config tag.gpgsign true
```

GitHub docs: <https://docs.github.com/authentication/managing-commit-signature-verification>.

## DCO sign-off

Conventional Commits + DCO trailer on every commit:

```bash
git commit -s -m "feat(engine): support FOK time-in-force"
git config alias.cm 'commit -s'  # optional
```
