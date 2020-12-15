#!/bin/bash
if [ $# != 2 ]
then
        echo "Usage: $0 <device name> <domain-id with frontend>"
else
       xenstore-write /local/domain/${2}/device/${1}/0/state 5
fi
