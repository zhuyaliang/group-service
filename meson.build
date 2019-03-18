project('group-service', 'c',
  version : '1.1.0',
  license : 'GPLv3',
  meson_version : '>=0.50.0',
  default_options : [ 'warning_level=1', 'buildtype=debugoptimized', 'c_std=gnu11'],
  )

gas_id = 'gas'
gas_name = meson.project_name()
gas_api_version = '1.1'
gas_api_name = '@0@-@1@'.format(gas_name, gas_api_version)
gas_prefix = get_option('prefix')
gas_datadir = join_paths(gas_prefix, get_option('datadir'))
gas_namespace = 'org.group.admin'
data_dir = join_paths(meson.current_source_dir(), 'data')
po_dir = join_paths(meson.current_source_dir(), 'po')
gas_libexecdir = join_paths(gas_prefix, get_option('libexecdir'))
gas_includedir = join_paths(gas_prefix, get_option('includedir'))
gas_pkgincludedir = join_paths(gas_includedir, gas_api_name)
gas_sysconfdir = join_paths(gas_prefix, get_option('sysconfdir'))
soversion = 1
current = 1
revision = 0
libversion = '@0@.@1@.@2@'.format(soversion, current, revision)
dbus_dep = dependency('dbus-1')
dbus_conf_dir = join_paths(dbus_dep.get_pkgconfig_variable('sysconfdir', define_variable: ['sysconfdir', gas_sysconfdir]), 'dbus-1', 'system.d')
dbus_ifaces_dir = dbus_dep.get_pkgconfig_variable('interfaces_dir', define_variable: ['datadir', gas_datadir])
dbus_sys_dir = dbus_dep.get_pkgconfig_variable('system_bus_services_dir', define_variable: ['datadir', gas_datadir])

systemd_system_unit_dir = get_option('systemdsystemunitdir')
install_systemd_unit_dir = (systemd_system_unit_dir != 'no')
enable_systemd = get_option('systemd')
enable_elogind = get_option('elogind')

if install_systemd_unit_dir and systemd_system_unit_dir == ''
  systemd_dep = dependency('systemd', required: false)
  assert(systemd_dep.found(), 'systemd required but not found, please provide a valid systemd user unit dir or disable     it')
  systemd_system_unit_dir = systemd_dep.get_pkgconfig_variable('systemdsystemunitdir')
endif

# Compiler & Project arguments
add_project_arguments([
  '-DHAVE_CONFIG_H',
  '-I' + meson.build_root(),
  ], language: 'c')

# get suported warning flags
test_args = [
  '-fstack-protector-strong',
  '-Waggregate-return',
  '-Warray-bounds',
  '-Wcast-align',
  '-Wclobbered',
  '-Wdeclaration-after-statement',
  '-Wempty-body',
  '-Wextra',
  '-Wformat=2',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Wformat-signedness',
  '-Wignored-qualifiers',
  #  '-Wimplicit-function-declaration',
  '-Winit-self',
  '-Winline',
  '-Wmissing-declarations',
  '-Wmissing-format-attribute',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wmissing-parameter-type',
  '-Wmissing-prototypes',
  '-Wnested-externs',
  '-Wno-discarded-qualifiers',
  '-Wno-missing-field-initializers',
  '-Wno-strict-aliasing',
  '-Wno-suggest-attribute=format',
  '-Wno-unused-parameter',
  '-Wold-style-definition',
  '-Woverride-init',
  '-Wpacked',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wreturn-type',
  '-Wshadow',
  '-Wsign-compare',
  '-Wstrict-aliasing',
  '-Wstrict-prototypes',
  '-Wswitch-default',
  '-Wtype-limits',
  '-Wundef',
  '-Wuninitialized',
  '-Wunused-but-set-variable',
  '-Wwrite-strings'
  ]

cc = meson.get_compiler('c')

foreach arg: test_args
  if cc.has_argument(arg)
    add_project_arguments(arg, language : 'c')
  endif
endforeach

# enable full RELRO where possible
# FIXME: until https://github.com/mesonbuild/meson/issues/1140 is fixed
global_link_args = []
test_link_args = [
  '-Wl,-z,relro',
  '-Wl,-z,now',
  ]
foreach arg: test_link_args
  if cc.has_argument(arg)
    global_link_args += arg
  endif
endforeach
add_global_link_arguments(
  global_link_args,
  language: 'c'
  )

# Dependencies
gio_dep = dependency('gio-2.0', version: '>= 2.37.3')
gio_unix_dep = dependency('gio-unix-2.0')
glib_dep = dependency('glib-2.0', version: '>= 2.44')
polkit_gobject_dep = dependency('polkit-gobject-1')
crypt_dep = cc.find_library('crypt')
# Configure data
policy_dir = polkit_gobject_dep.get_pkgconfig_variable('policydir', define_variable: ['prefix', gas_prefix])
conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('PACKAGE_NAME', meson.project_name())
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf.set_quoted('LUNAR_CALENDAR_LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))
conf.set_quoted('LIBDIR', join_paths(get_option('prefix'), get_option('libdir')))
conf.set_quoted('BINDIR', join_paths(get_option('prefix'), get_option('bindir')))
conf.set_quoted('SYSCONFDIR', join_paths(get_option('prefix'), get_option('sysconfdir')))
conf.set_quoted('LOCALSTATEDIR', join_paths(get_option('prefix'), get_option('localstatedir')))
conf.set_quoted('LIBEXECDIR', join_paths(get_option('prefix'), get_option('libexecdir')))

configure_file(
  output : 'config.h',
  configuration : conf
  )

top_srcdir = include_directories('.')
src_subdir = include_directories('.', 'src')


subdir('po')
subdir('src')
subdir('data')
subdir('test')
