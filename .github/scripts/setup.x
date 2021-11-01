#!/bin/bash
##
# setup.x - Mochimo Node setup script
#
# Copyright (c) 2021 Adequate Systems, LLC. All Rights Reserved.
# For more information, please refer to ../LICENSE
#
# Date: 1 November 2021
#

### Update/Install dependencies
apt update && apt install -y build-essential git-all

### Create Mochimo Relaynode Service
cat <<EOF >/etc/systemd/system/mochimo.service
# Contents of /etc/systemd/system/mochimo.service
[Unit]
Description=Mochimo Relay Node
After=network.target

[Service]
User=mochimo-node
Group=mochimo-node
ExecStart=/bin/bash /home/mochimo-node/mochimo/bin/gomochi d -n -D

[Install]
WantedBy=multi-user.target
EOF

### Reload service daemon and enable Mochimo Relaynode Service
systemctl daemon-reload
systemctl enable mochimo.service

### Create mochimo user
useradd -m -d /home/mochimo-node -s /bin/bash mochimo-node

### Switch to mochimo-node user
su mochimo-node

### Change directory to $HOME and download Mochimo Software
cd ~ && git clone --single-branch https://github.com/mochimodev/mochimo.git
cd ~/mochimo/src && ./makeunx bin -DCPU && ./makeunx install
cd ~ && cp ~/mochimo/bin/maddr.mat ~/mochimo/bin/maddr.dat

### Switch user back to original and reboot
exit
reboot

