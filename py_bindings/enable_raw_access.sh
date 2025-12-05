#!/bin/bash
sudo setcap 'cap_net_raw,cap_net_admin=+ep' $(readlink -f $(which python))
