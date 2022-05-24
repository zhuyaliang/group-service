## Tuglie

![admin:](https://github.com/zhuyaliang/images/blob/master/004.png)
![group:](https://github.com/zhuyaliang/images/blob/master/005.png)

## Describe
```
Using Dbus to manage user groups, you can complete the addition and deletion of user 
groups, add \ remove users to groups, and change the name of user groups.The `test` 
directory is some testing process.
```
## Part API Description
```
GasGroupManager *gas_group_manager_get_default() Create GasGroupManager classes

GSList          *gas_group_manager_list_groups(GasGroupManager *manager) Get classes GasGroup for all groups

const char      *gas_group_get_group_name(GasGroup *Group)      Get the group name

void             gas_group_set_group_name(GasGroup   *group,
                                          const char *name);    Modify the group name
```
## Compile

```
meson build -Dprefix=/usr
ninja -C build
sudo ninja -C build install
```

## Create deb package on Ubuntu MATE 22.04 LTS

```
sudo apt-get update
sudo apt-get install dpkg-dev debhelper-compat meson cmake pkg-config libdbus-1-dev systemd libglib2.0-dev libpolkit-gobject-1-dev polkitd

dpkg-buildpackage -uc -us
sudo apt-get install ../group-service*.deb
```
