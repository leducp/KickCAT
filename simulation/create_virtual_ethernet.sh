#!/bin/bash

# constants
RED='\033[1;31m'
NC='\033[0m' # No Color

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
        printError "No interface name supplied"
        help
        exit 1
    fi
}

printError()
{
    printf "${RED}${1}${NC}\n"
}

if [ -z "$command" ]; then
    printError "No command supplied."
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
        echo "Create a veth pair ${2}A >===< ${2}B successfully."
        exit 0
    else
        echo "Cannot create a veth pair ${2}A >===< ${2}B. Aborting"
        exit 2
    fi
elif [ "$command" == "delete" ]; then
    checkName
    if $(ip link del ${2}A); then
        echo "Delete the veth pair ${2}A >===< ${2}B successfully."
        exit 2
    else
        echo "Cannot delete the veth pair ${2}A >===< ${2}B. Aborting"
        exit 3
    fi
else
    printError "Invalid command."
    help
    exit 1
fi
