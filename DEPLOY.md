# Deploy PokerVariants on a Namecheap VPS

This guide deploys the C++ server on a Namecheap **VPS** running Ubuntu 22.04 or
24.04. The result is:

```
Browser --HTTPS/WSS--> Nginx :443 --HTTP/WS--> 127.0.0.1:8080 (pokerserver)
```

The C++ process is long-running and serves HTTP plus WebSockets. It must run
behind a reverse proxy with TLS; do not expose port 8080 to the internet.

This deployment serves the same app on both production domains:

- `variancepoker.live` and `www.variancepoker.live`
- `variancepoker.online` and `www.variancepoker.online`

The VPS address is `63.250.41.224`.

## Before you start

You need:

- A Namecheap VPS with root SSH access at `63.250.41.224`.
- Ubuntu 22.04 or 24.04 installed on the VPS.
- Both domains on Namecheap BasicDNS.
- Ports 22, 80, and 443 open.
- This repository on your local machine.

Set these values in your local shell, and again after you SSH into the VPS.
Replace the admin email with an address you control:

```sh
export PRIMARY_DOMAIN="variancepoker.live"
export SECONDARY_DOMAIN="variancepoker.online"
export SERVER_IP="63.250.41.224"
export ADMIN_EMAIL="you@example.com"
```

> **Namecheap shared hosting will not work.** Stellar, Stellar Plus, EasyWP, and
> other cPanel/WordPress plans do not provide a persistent root process where you
> can run an arbitrary C++ daemon and WebSocket server. Renaming the binary to
> `.cgi` does not make it a CGI program. Use a Namecheap VPS (or another VPS).

## 1. Point DNS at the VPS

In the Namecheap dashboard, configure both domains. For each domain:

1. Open **Domain List** and click **Manage**.
2. Open **Advanced DNS**.
3. Add these four A records in total:

   | Domain | Type | Host | Value | TTL |
   | --- | --- | --- | --- | --- |
   | variancepoker.live | A Record | `@` | `63.250.41.224` | Automatic |
   | variancepoker.live | A Record | `www` | `63.250.41.224` | Automatic |
   | variancepoker.online | A Record | `@` | `63.250.41.224` | Automatic |
   | variancepoker.online | A Record | `www` | `63.250.41.224` | Automatic |

4. Remove any existing URL Redirect (including the domain-level **Redirect
   Domain** setting), parking page, or conflicting `A`/`CNAME` records for both
   domains. Do not use a CNAME for the bare `@` hostname.

> If either domain resolves to `162.255.119.32` or
> `parkingpage.namecheap.com`, it is still using Namecheap parking or URL
> forwarding. Remove those records before continuing.

Verify from your local machine:

```sh
for host in \
  "$PRIMARY_DOMAIN" "www.$PRIMARY_DOMAIN" \
  "$SECONDARY_DOMAIN" "www.$SECONDARY_DOMAIN"; do
  printf '%-32s %s\n' "$host" "$(dig +short A "$host" | tr '\n' ' ')"
done
```

All four hostnames should return `63.250.41.224`. If you use Cloudflare in front
of the VPS, set it to **DNS only** until after the certificate is issued; a proxy
can interfere with the HTTP challenge.

## 2. Prepare the VPS

Connect as root:

```sh
ssh root@$SERVER_IP
```

Update the system and install the required packages:

```sh
apt update
apt -y upgrade
apt install -y build-essential git nginx certbot python3-certbot-nginx ufw rsync
```

Create a non-root service account and the application directory:

```sh
id poker >/dev/null 2>&1 || \
  useradd --system --home-dir /opt/pokervariants --shell /usr/sbin/nologin poker

install -d -o root -g root -m 0755 /opt/pokervariants
```

Configure the firewall. Only SSH and Nginx should be reachable:

```sh
ufw allow OpenSSH
ufw allow 'Nginx Full'
ufw --force enable
ufw status verbose
```

Do **not** run `ufw allow 8080`. Port 8080 is only used on localhost by Nginx.

If your Namecheap VPS has an additional firewall in the control panel, allow
22/tcp, 80/tcp, and 443/tcp there as well, and block 8080.

## 3. Copy and build the application

From your **local machine**, copy the project to the VPS. The exclusions keep
local git data, tests, old binaries, and private keys off the server.

```sh
cd /path/to/pokervariants
rsync -av --delete \
  --exclude .git \
  --exclude test \
  --exclude 'test/__pycache__' \
  --exclude pokerserver \
  --exclude pokerserver.cgi \
  --exclude poker \
  --exclude poker.pub \
  ./ root@$SERVER_IP:/opt/pokervariants/
```

> Never upload private SSH keys. The `poker`/`poker.pub` files in this working
> directory are excluded above and are not needed by the web app.

If the code is in a git repository, cloning is also fine:

```sh
ssh root@$SERVER_IP
rm -rf /opt/pokervariants
git clone <YOUR_REPOSITORY_URL> /opt/pokervariants
```

Back on the VPS, compile and set read-only ownership:

```sh
cd /opt/pokervariants
make clean
make

test -x pokerserver
test -f public/index.html
test -f deploy/pokervariants.service

chown -R root:root /opt/pokervariants
find /opt/pokervariants -type d -exec chmod 755 {} +
find /opt/pokervariants -type f -exec chmod 644 {} +
chmod 755 /opt/pokervariants/pokerserver
```

The server finds static files next to the executable (`/opt/pokervariants/public`),
so keep `pokerserver` and `public/` together.

Test the backend locally before starting the service:

```sh
sudo -u poker timeout 5 /opt/pokervariants/pokerserver 8080 \
  >/tmp/pokervariants-test.log 2>&1 &
PV_PID=$!
sleep 1
curl -fsS http://127.0.0.1:8080/api/new
echo
kill "$PV_PID" 2>/dev/null || true
wait "$PV_PID" 2>/dev/null || true
cat /tmp/pokervariants-test.log
```

You should see a JSON response containing `id` and `hostKey`. The temporary
table disappears when the test process exits.

## 4. Install the systemd service

The included unit assumes the path `/opt/pokervariants`, the `poker` user, and
port 8080.

```sh
install -m 0644 /opt/pokervariants/deploy/pokervariants.service \
  /etc/systemd/system/pokervariants.service

systemctl daemon-reload
systemctl enable --now pokervariants.service
systemctl status pokervariants.service --no-pager
journalctl -u pokervariants.service -n 50 --no-pager
```

systemd does not load unit files directly from `/opt/pokervariants/deploy`.
Verify that the installed copy is visible:

```sh
ls -l /etc/systemd/system/pokervariants.service
systemctl cat pokervariants.service
```

If `systemctl` reports `Unit pokervariants.service not found`, see the
troubleshooting section below.

Check that it is listening locally:

```sh
ss -ltnp | grep 8080
```

The output should show `127.0.0.1:8080` or `0.0.0.0:8080` owned by the `poker`
process. External access to 8080 is blocked by UFW. To restart or inspect it:

```sh
systemctl restart pokervariants
journalctl -u pokervariants -f
```

## 5. Install the Nginx reverse proxy

The included Nginx config proxies both ordinary HTTP and the table WebSocket.
It also uses a log format that omits query strings, so host keys passed as
`?k=...` on the WebSocket URL are not written to the access log.

The included config already lists both production domains and their `www`
hostnames. Install it directly:

```sh
cd /opt/pokervariants
install -m 0644 deploy/nginx-pokervariants.conf \
  /etc/nginx/sites-available/pokervariants.conf

ln -sf /etc/nginx/sites-available/pokervariants.conf \
  /etc/nginx/sites-enabled/pokervariants.conf

# Only remove this if you are not using the default site for anything else.
rm -f /etc/nginx/sites-enabled/default

nginx -t
systemctl reload nginx
```

Test plain HTTP on both domains (TLS comes in the next step):

```sh
curl -I "http://$PRIMARY_DOMAIN"
curl -I "http://$SECONDARY_DOMAIN"
```

## 6. Enable HTTPS and WSS

Run Certbot after all four DNS names resolve and Nginx is serving HTTP. This
issues one certificate covering both domains and their `www` hostnames:

```sh
certbot --nginx \
  -d "$PRIMARY_DOMAIN" -d "www.$PRIMARY_DOMAIN" \
  -d "$SECONDARY_DOMAIN" -d "www.$SECONDARY_DOMAIN" \
  --redirect --agree-tos --no-eff-email \
  -m "$ADMIN_EMAIL"
```

Certbot edits the Nginx site to add TLS and redirect HTTP to HTTPS. Confirm that
renewal works:

```sh
certbot renew --dry-run
systemctl list-timers | grep certbot
```

Verify HTTPS on both domains:

```sh
curl -I "https://$PRIMARY_DOMAIN"
curl -I "https://$SECONDARY_DOMAIN"
curl -I "https://www.$PRIMARY_DOMAIN"
curl -I "https://www.$SECONDARY_DOMAIN"
```

Open `https://variancepoker.live` and `https://variancepoker.online` in a browser.
The frontend automatically uses `wss://` on HTTPS pages, so the WebSocket goes
through the same Nginx proxy.

## 7. End-to-end verification

From the deployed site:

1. Create a table with a name and starting stack.
2. Confirm the URL looks like `https://variancepoker.live/#/t/<tableId>`
   (or the same path on `variancepoker.online`).
3. Copy the link and open it on a phone or another network.
4. Request a seat with a different name and stack.
5. Approve the request from the host's **Host settings** panel.
6. Start the game, play a hand, and send a chat message from each client.
7. In browser developer tools, confirm the WebSocket connection is `wss://`
   and stays open.
8. Check that port 8080 is not reachable from outside the VPS.
9. Confirm host keys are absent from Nginx access logs:

   ```sh
   grep 'k=' /var/log/nginx/pokervariants.access.log || echo "no host keys found"
   ```

## 8. Updating the deployment

All table state is held in memory. Restarting the process deletes every table,
so deploy during a quiet period.

From your local machine:

```sh
rsync -av --delete \
  --exclude .git \
  --exclude test \
  --exclude 'test/__pycache__' \
  --exclude pokerserver \
  --exclude pokerserver.cgi \
  --exclude poker \
  --exclude poker.pub \
  ./ root@$SERVER_IP:/opt/pokervariants/
```

Then on the VPS:

```sh
systemctl stop pokervariants
cd /opt/pokervariants
make clean
make
chown -R root:root /opt/pokervariants
find /opt/pokervariants -type d -exec chmod 755 {} +
find /opt/pokervariants -type f -exec chmod 644 {} +
chmod 755 /opt/pokervariants/pokerserver
systemctl start pokervariants
systemctl status pokervariants --no-pager
```

If deploying with git instead:

```sh
cd /opt/pokervariants
git pull --ff-only
make clean && make
systemctl restart pokervariants
```

Things worth backing up are the code, `/etc/nginx/sites-available/pokervariants.conf`,
and `/etc/systemd/system/pokervariants.service`. There is no database and no hand
or account persistence. Long-running processes also keep old tables in memory;
restart the service occasionally during quiet periods to reclaim memory.

## 9. Troubleshooting

### `Unit pokervariants.service not found`

The unit file is still in the repository's `deploy/` directory, or the install
command was not run as root. systemd only searches `/etc/systemd/system`,
`/usr/lib/systemd/system`, and `/lib/systemd/system`; it does not search
`/opt/pokervariants/deploy`.

Check both paths:

```sh
ls -l /opt/pokervariants/deploy/pokervariants.service
ls -l /etc/systemd/system/pokervariants.service
```

If only the first file exists, install it and reload systemd:

```sh
install -m 0644 /opt/pokervariants/deploy/pokervariants.service \
  /etc/systemd/system/pokervariants.service
systemctl daemon-reload
systemctl enable --now pokervariants.service
systemctl status pokervariants.service --no-pager
```

If the source file is missing, copy the `deploy/` directory from your local
checkout:

```sh
# Run this on your local machine, replacing the VPS address.
rsync -av deploy/ root@$SERVER_IP:/opt/pokervariants/deploy/
```

Then repeat the install commands above.

Alternatively, create the unit directly on the VPS:

```sh
sudo tee /etc/systemd/system/pokervariants.service >/dev/null <<'EOF'
[Unit]
Description=PokerVariants C++ server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=poker
Group=poker
WorkingDirectory=/opt/pokervariants
ExecStart=/opt/pokervariants/pokerserver 8080
Restart=on-failure
RestartSec=2s
KillSignal=SIGTERM
TimeoutStopSec=10s
UMask=0027

NoNewPrivileges=true
PrivateTmp=true
PrivateDevices=true
ProtectSystem=strict
ProtectHome=true
ProtectKernelTunables=true
ProtectKernelModules=true
ProtectControlGroups=true
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX
RestrictSUIDSGID=true
LockPersonality=true
LimitNOFILE=65536

StandardOutput=journal
StandardError=journal
SyslogIdentifier=pokervariants

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable --now pokervariants.service
sudo systemctl status pokervariants.service --no-pager
```

Before starting it, confirm that `/opt/pokervariants/pokerserver` exists and is
executable, and that the `poker` user exists:

```sh
ls -l /opt/pokervariants/pokerserver
id poker
```

Confirm the filename is exactly `pokervariants.service` and that you are running
`systemctl` on the VPS, not on your local computer. If `id -u` is not `0`, use
`sudo` for the install commands.

### `Server Not Found` or the domain still shows a Namecheap parking page

The request is not reaching the VPS. Check all four names:

```sh
for host in \
  "$PRIMARY_DOMAIN" "www.$PRIMARY_DOMAIN" \
  "$SECONDARY_DOMAIN" "www.$SECONDARY_DOMAIN"; do
  printf '%-32s %s\n' "$host" "$(dig +short A "$host" | tr '\n' ' ')"
done
```

Every hostname must return `63.250.41.224`. In particular:

- `variancepoker.live` or `www.variancepoker.live` returning `162.255.119.32`
  or `parkingpage.namecheap.com` means Namecheap URL Forwarding or parking is
  still active. Delete those records in **Advanced DNS**.
- `variancepoker.online` returning nothing means its A records were never saved.
- Remove conflicting CNAME records for the same hosts and wait for DNS
  propagation before retrying.

### `502 Bad Gateway`

The proxy cannot reach the backend.

```sh
systemctl status pokervariants
journalctl -u pokervariants -n 100 --no-pager
ss -ltnp | grep 8080
```

Check that the service is running, the port matches the Nginx config, and the
`poker` user can read `/opt/pokervariants/pokerserver` and `public/`.

### Static files return 404

The server looks for `public/` relative to the binary and then relative to the
working directory. Confirm:

```sh
ls -l /opt/pokervariants/pokerserver /opt/pokervariants/public/index.html
```

### WebSocket fails or disconnects immediately

- Confirm the page is loaded over HTTPS and the browser is using `wss://`.
- Confirm `deploy/nginx-pokervariants.conf` is installed; the `Upgrade` and
  `Connection` headers are required.
- Run `nginx -t` and reload Nginx.
- Check browser developer tools and `/var/log/nginx/pokervariants.error.log`.

### Certbot cannot validate the domains

- Confirm all four A records return `63.250.41.224` with `dig`.
- Confirm port 80 is open in both UFW and the Namecheap VPS firewall.
- Turn off Cloudflare proxying, URL forwarding, and parking pages during issuance.
- Retry:

  ```sh
  certbot --nginx \
    -d "$PRIMARY_DOMAIN" -d "www.$PRIMARY_DOMAIN" \
    -d "$SECONDARY_DOMAIN" -d "www.$SECONDARY_DOMAIN" \
    --redirect
  ```

### `Address already in use` on port 8080

Another process is already using the port:

```sh
ss -ltnp | grep 8080
```

Stop the old process or change the port in both the systemd unit and the Nginx
`proxy_pass` line.

### Permission denied

The service runs as `poker`, not root. Check access:

```sh
sudo -u poker test -r /opt/pokervariants/pokerserver && echo binary-ok
sudo -u poker test -r /opt/pokervariants/public/index.html && echo public-ok
```

### Everything disappears after a restart

That is expected. Tables, chat, hand history, and host keys are in memory only.
The host key is also stored in the host browser's session storage, so a restart
cannot recover a table.

## 10. Security notes

- There are no accounts. Anyone with a table link can request a seat; the host
  must approve the request.
- Keep TLS enabled. Do not run the site over plain HTTP in production.
- Keep port 8080 firewalled. Nginx is the only public entry point.
- The service runs as the unprivileged `poker` user, and the application files are
  root-owned and read-only to that user.
- The Nginx access-log format intentionally excludes query strings so the host
  key is not logged. Do not change it to `$request` or `$request_uri`.
- The app has no built-in rate limiting. If the site is public and abused, add
  Nginx `limit_req` rules or put a firewall/WAF in front of it.
