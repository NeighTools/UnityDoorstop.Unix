#define _GNU_SOURCE

#include <string.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <libgen.h>
#include "plthook.h"

#define TRUE 1
#define FALSE 0
#define F_OK 0

#if __linux__
// rely on GNU for now (which we need anyway for LD_PRELOAD trick)
extern char *program_invocation_name;

#define PATH_MAX 4096 // Maximum on Linux
#define program_path(app_path) realpath(program_invocation_name, app_path)

#elif __APPLE__
#include <mach-o/dyld.h>

#define PATH_MAX 1024 // Maximum on macOS
#define program_path(app_path)                    \
    {                                             \
        uint32_t bufsize = PATH_MAX;              \
        _NSGetExecutablePath(app_path, &bufsize); \
    }
#endif

void *(*r_mono_jit_init_version)(const char *root_domain_name, const char *runtime_version);
void *(*r_mono_domain_assembly_open)(void *domain, const char *name);
void *(*r_mono_assembly_get_image)(void *assembly);
void *(*r_mono_runtime_invoke)(void *method, void *obj, void **params, void **exc);

void *(*r_mono_method_desc_new)(const char *name, int include_namespace);
void *(*r_mono_method_desc_search_in_image)(void *desc, void *image);
void *(*r_mono_method_signature)(void *method);
uint32_t (*r_mono_signature_get_param_count)(void *sig);

void *(*r_mono_array_new)(void *domain, void *eclass, uintptr_t n);
void (*r_mono_gc_wbarrier_set_arrayref)(void *arr, void *slot_ptr, void *value);
char *(*r_mono_array_addr_with_size)(void *arr, int size, uintptr_t idx);

void *(*r_mono_get_string_class)();
void *(*r_mono_string_new)(void *domain, const char *text);

void *(*r_mono_thread_current)();
void (*r_mono_thread_set_main)(void *thread);
void (*r_mono_domain_set_config)(void *domain, char *base_dir, char *config_file_name);
char *(*r_mono_assembly_getrootdir)();


void doorstop_init_mono_functions(void *handle)
{
#define LOAD_METHOD(m) r_##m = dlsym(handle, #m)

    LOAD_METHOD(mono_jit_init_version);
    LOAD_METHOD(mono_domain_assembly_open);
    LOAD_METHOD(mono_assembly_get_image);
    LOAD_METHOD(mono_runtime_invoke);
    LOAD_METHOD(mono_method_desc_new);
    LOAD_METHOD(mono_method_desc_search_in_image);
    LOAD_METHOD(mono_method_signature);
    LOAD_METHOD(mono_signature_get_param_count);
    LOAD_METHOD(mono_array_new);
    LOAD_METHOD(mono_gc_wbarrier_set_arrayref);
    LOAD_METHOD(mono_array_addr_with_size);
    LOAD_METHOD(mono_get_string_class);
    LOAD_METHOD(mono_string_new);
    LOAD_METHOD(mono_thread_current);
    LOAD_METHOD(mono_thread_set_main);
    LOAD_METHOD(mono_domain_set_config);
    LOAD_METHOD(mono_assembly_getrootdir);

#undef LOAD_METHOD
}

void *jit_init_hook(const char *root_domain_name, const char *runtime_version)
{
    // Call the original r_mono_jit_init_version to initialize the Unity Root Domain
    void *domain = r_mono_jit_init_version(root_domain_name, runtime_version);

    if (strcmp(getenv("DOORSTOP_ENABLE"), "TRUE"))
    {
        printf("[Doorstop] DOORSTOP_ENABLE is not TRUE! Disabling Doorstop...\n");
        return domain;
    }
    if (getenv("DOORSTOP_INITIALIZED"))
    {
        printf("DOORSTOP_INITIALIZED is set! Skipping!\n");
        return domain;
    }
    setenv("DOORSTOP_INITIALIZED", "TRUE", TRUE);

    if (r_mono_domain_set_config) 
    {
        char app_path[PATH_MAX] = "\0";
        char config_path[PATH_MAX] = "\0";
        program_path(app_path);
        strcpy(config_path, app_path);

        strcat(config_path, ".config");
        char *folder_path = dirname(app_path);

        printf("Setting config paths; basedir: %s; config: %s\n", folder_path, config_path);
        r_mono_domain_set_config(domain, folder_path, config_path);
    }

    char *dll_path = getenv("DOORSTOP_INVOKE_DLL_PATH");

    char *assembly_dir = r_mono_assembly_getrootdir();
    printf("Managed dir: %s\n", assembly_dir);
    setenv("DOORSTOP_MANAGED_FOLDER_DIR", assembly_dir, TRUE);
    free(assembly_dir);

    char app_path[PATH_MAX] = "\0";
    program_path(app_path);
    setenv("DOORSTOP_PROCESS_PATH", app_path, TRUE);

    // Load our custom assembly into the domain
    void *assembly = r_mono_domain_assembly_open(domain, dll_path);

    if (assembly == NULL)
    {
        printf("Failed to load assembly\n");
        return domain;
    }

    // Get assembly's image that contains CIL code
    void *image = r_mono_assembly_get_image(assembly);

    printf("Got image: %p \n", image);

    if (image == NULL)
    {
        printf("Failed to locate the image!\n");
        return domain;
    }

    // Note: we use the runtime_invoke route since jit_exec will not work on DLLs

    // Create a descriptor for a random Main method
    void *desc = r_mono_method_desc_new("*:Main", FALSE);

    // Find the first possible Main method in the assembly
    void *method = r_mono_method_desc_search_in_image(desc, image);

    if (method == NULL)
    {
        printf("Failed to locate any entrypoints!\n");
        return domain;
    }

    void *signature = r_mono_method_signature(method);

    // Get the number of parameters in the signature
    uint32_t params = r_mono_signature_get_param_count(signature);

    void **args = NULL;
    if (params == 1)
    {
        // If there is a parameter, it's most likely a string[].
        void *args_array = r_mono_array_new(domain, r_mono_get_string_class(), 2);

        args = malloc(sizeof(void *) * 1);
        args[0] = args_array;
    }

    r_mono_runtime_invoke(method, NULL, args, NULL);

    if (args != NULL)
    {
        free(args);
        args = NULL;
    }

    return domain;
}

void *dlsym_hook(void *handle, const char *name) {
    
    if (!strcmp(name, "mono_jit_init_version"))
    {
        doorstop_init_mono_functions(handle);
        return (void *)jit_init_hook;
    }

    return dlsym(handle, name);
}

int fclose_hook(FILE *stream) {
    // Some versions of Unity wrongly close stdout, which prevents writing to console
    if (stream == stdout)
        return F_OK;
    return fclose(stream);
}

__attribute__ ((constructor)) void doorstop_setup() {
    plthook_t *hook;
    
    // Some versions of Unity (especially macOS) ship with UnityPlayer shared lib
    void *unity_player = plthook_handle_by_name("UnityPlayer");

#if __linux__
    if(!unity_player) {
        // In some newer Unity versions, UnityPlayer is a separate lib
        unity_player = dlopen("UnityPlayer.so", RTLD_LAZY);
    }
#endif

    if(unity_player && plthook_open_by_handle(&hook, unity_player) == 0) {
        printf("Found UnityPlayer, hooking into it instead\n");
    }
    else if(plthook_open(&hook, NULL) != 0) {
        printf("Failed to open current process PLT! Cannot run Doorstop! Error: %s\n", plthook_error());
        return;
    }

    if(plthook_replace(hook, "dlsym", &dlsym_hook, NULL) != 0)
        printf("Failed to hook dlsym, ignoring it. Error: %s\n", plthook_error());

    if(plthook_replace(hook, "fclose", &fclose_hook, NULL) != 0)
        printf("Failed to hook fclose, ignoring it. Error: %s\n", plthook_error());

#if __APPLE__
    /*
        On older Unity versions, Mono methods are resolved by the OS's loader directly.
        Because of this, there is no dlsym, in which case we need to apply a PLT hook.
    */
    void *mono_handle = plthook_handle_by_name("libmono");

    if(plthook_replace(hook, "mono_jit_init_version", &jit_init_hook, NULL) != 0)
        printf("Failed to hook jit_init_version, ignoring it. Error: %s\n", plthook_error());
    else if(mono_handle)
        doorstop_init_mono_functions(mono_handle);
#endif

    plthook_close(hook);
}