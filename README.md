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



