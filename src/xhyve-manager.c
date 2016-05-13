/**
 * xhyve-manager
 * a simple CLI utility to manage xhyve virtual machines.
 *
 * Usage: ./xhyve-manager \
 *   + {list} all available machines
 *   + {create,delete,start} <machine-name>
 *
 **/

// System
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <pwd.h>
#include <assert.h>
#include <uuid/uuid.h>

// xhyve
#include <xhyve/xhyve.h>

// Constants
#define DEFAULT_VM_DIR "xhyve VMs"
#define DEFAULT_VM_EXT "xhyvm"

// Local
#include <xhyve-manager/xhyve-manager.h>
#include <ini/ini.h>

static int handler(void* machine, const char* section, const char* name,
                   const char* value)
{
  xhyve_virtual_machine_t *pconfig = (xhyve_virtual_machine_t *)machine;

  if (0) ;
#define CFG(s, n, default) else if (strcmp(section, #s)==0 && \
                                    strcmp(name, #n)==0) pconfig->s##_##n = strdup(value);
#include <xhyve-manager/config.def>

  return 1;
}

char *get_machine_path(const char *machine_name)
{
  char *machine_path = NULL;
  asprintf(&machine_path, "%s/%s/%s.%s", get_homedir(), DEFAULT_VM_DIR, machine_name, DEFAULT_VM_EXT);
  return machine_path;
}

int start_machine(xhyve_virtual_machine_t *machine)
{
  char *acpiflag = "";
  char *pci_dev = "";
  char *pci_lpc = "";
  char *lpc_dev = "";
  char *net = "";
  char *img_cd = "";
  char *img_hdd = "";
  char *firmware = "";
  char *cdflag = NULL;

  chdir(get_machine_path(machine->machine_name));
  form_config_string(&pci_dev, "ss", machine->bridge_slot, machine->bridge_driver);
  form_config_string(&pci_lpc, "ss", machine->lpc_slot, machine->lpc_driver);
  form_config_string(&lpc_dev, "s", machine->lpc_configinfo);
  form_config_string(&net, "ss", machine->networking_slot, machine->networking_driver);

  if (strcmp(machine->external_storage_configinfo, "") != 0) {
    form_config_string(&img_cd, "sss",
                       machine->external_storage_slot,
                       machine->external_storage_driver,
                       machine->external_storage_configinfo);
    cdflag = "-s";
  }

  form_config_string(&img_hdd, "sss",
                     machine->internal_storage_slot,
                     machine->internal_storage_driver,
                     machine->internal_storage_configinfo);

  if (chdir(get_machine_path(machine->machine_name)) < 0)
    perror("chdir");

  if (strcmp(machine->machine_type, "linux") == 0) {
    acpiflag = NULL;
    form_config_string(&firmware, "ssss", "kexec", machine->boot_kernel, machine->boot_initrd, machine->boot_options);
  } else if (strcmp(machine->machine_type, "bsd") == 0) {
    acpiflag = "-A";
    form_config_string(&firmware, "ssss", "fbsd", "userboot.so", machine->boot_initrd, machine->boot_options);
  } else {
    fprintf(stderr, "Sorry, a %s OS is not supported. Did you mean 'linux' or 'bsd'?\n", machine->machine_type);
    exit(EXIT_FAILURE);
  }

  char *args[] = {
    "xhyve",
    "-U",
    machine->machine_uuid,
    "-f",
    firmware,
    "-m",
    machine->memory_size,
    "-c",
    machine->processor_cpus,
    "-s",
    pci_dev,
    "-s",
    pci_lpc,
    "-l",
    lpc_dev,
    "-s",
    net,
    "-s",
    img_hdd,
    acpiflag,
    cdflag,
    img_cd,
    NULL
  };

  int argnum = 0;
  char **ptr = NULL;
  ptr = args;

  while (*ptr) {
    ++argnum;
    ++ptr;
  }

  return xhyve_entrypoint(argnum, args);
}

void print_machine_info(xhyve_virtual_machine_t *machine)
{
#define CFG(s, n, default) printf("%s_%s = %s\n", #s, #n, machine->s##_##n);
#include <xhyve-manager/config.def>
}

char *get_config_path(const char *machine_name)
{
  char *config_path = NULL;
  asprintf(&config_path, "%s/config.ini", get_machine_path(machine_name));
  return config_path;
}

void load_machine_config(xhyve_virtual_machine_t *machine, const char *machine_name)
{
  if (ini_parse(get_config_path(machine_name), handler, machine) < 0) {
    fprintf(stderr, "Missing or invalid machine config at %s\n", get_config_path(machine_name));
    exit(EXIT_FAILURE);
  }
}

void edit_machine_config(xhyve_virtual_machine_t *machine)
{
  char *editor = NULL;

  if ((editor = getenv("EDITOR")) == NULL) {
    editor = "nano";
  }

  fprintf(stdout, "\nEditing %s config with external editor: %s\n", machine->machine_name, editor);

  pid_t child;
  if ((child = fork()) == -1) {
    perror("fork");
  } else {
    if (child > 0) {
      int status;
      waitpid(child, &status, 0);
      if (WIFEXITED(status)) {
        fprintf(stdout, "\nEdited configuration for %s machine\n", machine->machine_name);
        print_machine_info(machine);
      }
    } else {
      execlp(editor, editor, get_config_path(machine->machine_name), (const char *) NULL);
    }
  }
}

void parse_args(xhyve_virtual_machine_t *machine, const char *command, const char *param)
{
  if (command && param) {
    machine = malloc(sizeof(xhyve_virtual_machine_t));
    load_machine_config(machine, param);

    if (!strcmp(command, "info")) {
      print_machine_info(machine);
    } else if (!strcmp(command, "start")) {
      start_machine(machine);
    } else if (!strcmp(command, "edit")) {
      edit_machine_config(machine);
    }
  } else {
    print_usage();
  }
}

int print_usage(void)
{
  fprintf(stderr, "Usage: xhyve-manager <command> <machine-name>\n");
  fprintf(stderr, "\tcommands:\n");
  fprintf(stderr, "\t  info: show info about VM\n");
  fprintf(stderr, "\t  start: start VM (needs root)\n");
  fprintf(stderr, "\t  edit: edit the configuration for VM\n");
  exit(EXIT_FAILURE);
}

// <slot,driver,configinfo> PCI slot config

void form_config_string(char **ret, const char* fmt, ...)
{
  // From http://en.cppreference.com/w/c/variadic
  va_list args;
  va_start(args, fmt);

  asprintf(ret, "%s", "");
  const char *next;

  while (*fmt != '\0') {
    next = fmt + 1;

    if (*fmt == 's') {
      char *s = va_arg(args, char *);
      asprintf(ret, "%s%s", *ret, s);
      if (*next != '\0')
        asprintf(ret, "%s,", *ret);
    }
    ++fmt;
  }

  va_end(args);
}

const char *get_homedir(void)
{
  char *user = NULL;
  if (getuid() == 0) { // if root
    user = getenv("SUDO_USER");
  } else {
    user = getenv("USER");
  }

  struct passwd *pwd;
  pwd = getpwnam(user);
  return pwd->pw_dir;
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    print_usage();
  }

  char *command = argv[1];
  char *machine_name = argv[2];
  xhyve_virtual_machine_t *machine = NULL;

  if (machine_name) {
    parse_args(machine, command, machine_name);
    exit(EXIT_SUCCESS);
  } else {
    print_usage();
  }
}


