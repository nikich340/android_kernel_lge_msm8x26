#!/system/bin/sh
#Written by Vagelis1608 (xda-developers) for LollipopExtended Kernel by Nikita Pro Android (xda-developers)

#Fix init.d folder
mount -o rw,remount /system
if [ ! -e /system/etc/init.d ]; then
    mkdir /system/etc/init.d
fi
chmod -R 755 /system/etc/init.d
chown -R root.root /system/etc/init.d
mount -o ro,remount /system

#Start init.d
for FILE in /system/etc/init.d/*; do
    sh $FILE >/dev/null
done
