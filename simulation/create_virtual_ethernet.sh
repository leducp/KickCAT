#!/bin/bash

command=$1
interface=$2

if [ -z "$command" ]; then
    echo "No command supplied."
    exit 1
fi

if [ -z "$interface" ]; then
    echo "No interface name supplied"
    exit 1
fi

if [ "$command" == "create" ]; then
    if $(ip link add ${2}A type veth peer name ${2}B); then
        echo "Create a veth pair ${2}A >===< ${2}B successfully."
        exit 0
    else
        echo "Cannot create a veth pair ${2}A >===< ${2}B. Aborting"
        exit 2
    fi
elif [ "$command" == "delete" ]; then
    if $(ip link del ${2}A); then
        echo "Delete the veth pair ${2}A >===< ${2}B successfully."
        exit 2
    else
        echo "Cannot delete the veth pair ${2}A >===< ${2}B. Aborting"
        exit 3
    fi
else
    echo "Invalid command. Acceptable values are create or delete".
    exit 1
fi
