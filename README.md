## Tuglie

![admin:](https://github.com/zhuyaliang/images/blob/master/004.png)
![group:](https://github.com/zhuyaliang/images/blob/master/005.png)

## Describe
```
Using Dbus to manage user groups, you can complete the addition and deletion of user 
groups, add \ remove users to groups, and change the name of user groups.The `test` 
directory is some testing process.
```

## Compile

```
meson build -Dprefix=/usr
ninja -C build
sudo ninja -C build install



