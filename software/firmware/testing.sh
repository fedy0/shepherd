#!/bin/sh
# chmod +x testing.sh
# sudo ./testing.sh pru_iep.patch

# 0. Remove tools if they exist
<<comment
if [ $1 == "-r" ]
then
  echo "0. Removing tools..."
  rm -rf tools
  echo "0. Done!"
  exit
fi
comment

PATCH_FILE=$1
#echo $PATCH_FILE
sed -i -e "s/\(volatile __far\).*/\/\/patch >>/" $PWD/tools/pru-software-support-package/include/am335x/pru_iep.h
sed -i -e "/\/\/patch >>/r $1" $PWD/tools/pru-software-support-package/include/am335x/pru_iep.h
