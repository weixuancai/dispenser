# dispenser
<font color = "green">rc.local</font>
path:
```
/ect/rc.local
```

set config:
```

# By default this script does nothing

# add these
sleep 30
sudo ldconfig &

cd /home/tr001/SIGS-master-new-bakup/bin
sudo ./SGSmaster -a &
exit 0

```

<font color = "green">TeamViewer / SSH</font>
```
$ sudo apt-get install ssh

$ sudo dpkg -i teamviewer_xx.deb
$ sudo apt-get install -f
```


<font color = "blue">Folder List</font>

|Name|Sub-Folder/File|Function|
|-|-|-|
|SIGS-master-new-bakup|bin|excute file|
||conf|setting of DB/config|
||source|main program|
|||
||meto.txt|
||README. md|
||startSGS. sh|
||startSGSmaster. sh|

<font color = "blue">Excute</font>

:::info
 Compiler code
:::
```
$ cd ~/source/main
$ make
```
:::info
 Make sure kill the Queue first
 The perms=666 is SGSmaster
:::
```
$ ipcs
$ ipcrm -m {key_ID}
```

:::info
 Restart the program
:::
```
$ cd SIGS-master-new-backup/bin
$ sudo ./SGSmaster -a
```

:::warning
 In server, still have to run gunicorn
:::
```
$ gunicorn -b 0.0.0.0:10000 app:api --timeout 60
```
