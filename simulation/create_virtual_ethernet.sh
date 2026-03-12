#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

source "$SCRIPT_DIR/../scripts/lib/log.sh"

command=$1
interface=$2

help()
{
    echo "Helper script to manage a pair of virtual Ethernet sockets connected together (socketA >===< socketB)."
    echo "Available commands: "
    echo "    - help                     Display this message"
    echo "    - create [name]            Create a pair of socket A/B prefixed with 'name'"
    echo "    - delete [name]            Delete a pair of socket A/B prefixed with 'name'"
}

checkName()
{
    if [ -z "$interface" ]; then
        error "No interface name supplied"
        help
        exit 1
    fi
}

if [ -z "$command" ]; then
    error "No command supplied."
    help
    exit 1
fi

if [ "$command" == "help" ]; then
    help
    exit 0
fi

if [ "$command" == "create" ]; then
    checkName
    if $(ip link add ${2}A type veth peer name ${2}B); then
        success "Created veth pair ${2}A >===< ${2}B"
        exit 0
    else
        error "Cannot create veth pair ${2}A >===< ${2}B"
        exit 2
    fi
elif [ "$command" == "delete" ]; then
    checkName
    if $(ip link del ${2}A); then
        success "Deleted veth pair ${2}A >===< ${2}B"
        exit 0
    else
        error "Cannot delete veth pair ${2}A >===< ${2}B"
        exit 3
    fi
else
    error "Invalid command."
    help
    exit 1
fi
