# Runs on the target once the staging directory holds the package,
# the server config, the dnsmasq config, the systemd units and any
# loader binaries. deploy.py fills in the staging directory and the
# PXE subnet before sending it, the unit's user is filled in here.
set -e
ci_user="$(whoami)"
staging="$HOME/@STAGING@"
sudo mkdir -p /srv/ultra-ci/tftp /srv/ultra-ci/data
sudo rm -rf /srv/ultra-ci/baremetal_ci
sudo cp -r "$staging/baremetal_ci" /srv/ultra-ci/
sed "s/@PXE_SUBNET@/@SUBNET@/" "$staging/dnsmasq.conf" \
    > /tmp/ultra-ci-dnsmasq.conf
sudo mv /tmp/ultra-ci-dnsmasq.conf /srv/ultra-ci/dnsmasq.conf
sudo cp "$staging/config.json" /srv/ultra-ci/config.json
for f in BOOTX64.EFI hyper.pxe; do
    if [ -f "$staging/$f" ]; then
        sudo install -m 644 "$staging/$f" "/srv/ultra-ci/tftp/$f"
    fi
done
sudo chown -R "$ci_user" /srv/ultra-ci/data /srv/ultra-ci/tftp \
    /srv/ultra-ci/config.json
sudo chmod 600 /srv/ultra-ci/config.json
sudo sed "s/@CI_USER@/$ci_user/" "$staging/ultra-ci.service" \
    > /tmp/ultra-ci.service
sudo mv /tmp/ultra-ci.service /etc/systemd/system/
sudo cp "$staging/ultra-ci-dnsmasq.service" /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable ultra-ci-dnsmasq.service
sudo systemctl restart ultra-ci-dnsmasq.service
sudo systemctl enable ultra-ci.service
sudo systemctl restart ultra-ci.service
rm -rf "$staging"
