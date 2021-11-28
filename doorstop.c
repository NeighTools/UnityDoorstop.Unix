#define _GNU_SOURCE

#include <string.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <libgen.h>
#include <unistd.h>
#include <dirent.h>
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

void (*r_mono_config_parse)(const char *filename);
void (*r_mono_set_assemblies_path)(const char *path);

void *(*r_mono_image_open_from_data_with_name)(void *data, uint32_t data_len, int need_copy, void *status, int refonly, const char *name);

void *(*r_mono_object_to_string)(void *obj, void **exc);
char *(*r_mono_string_to_utf8)(void *s);

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
    LOAD_METHOD(mono_config_parse);
    LOAD_METHOD(mono_set_assemblies_path);
    LOAD_METHOD(mono_image_open_from_data_with_name);

    LOAD_METHOD(mono_object_to_string);
    LOAD_METHOD(mono_string_to_utf8);

#undef LOAD_METHOD
}

#define LOG(message)                        \
    {                                       \
        fprintf(stderr, "[Doorstop] ");     \
        fprintf(stderr, message);           \
                                            \
        fprintf(log_handle, "[Doorstop] "); \
        fprintf(log_handle, message);       \
                                            \
        fflush(log_handle);                 \
        fflush(stdout);                     \
    }

#define LOGF(message, ...)                         \
    {                                              \
        fprintf(log_handle, "[Doorstop] ");        \
        fprintf(log_handle, message, __VA_ARGS__); \
                                                   \
        fprintf(stderr, "[Doorstop] ");            \
        fprintf(stderr, message, __VA_ARGS__);     \
                                                   \
        fflush(log_handle);                        \
        fflush(stdout);                            \
    }

static FILE *log_handle;

void *jit_init_hook(const char *root_domain_name, const char *runtime_version)
{
    LOG("jit_init_hook invoked\n");

    char *override = getenv("DOORSTOP_CORLIB_OVERRIDE_PATH");
    DIR *override_dir = override ? opendir(override) : NULL;
    char *assembly_dir = r_mono_assembly_getrootdir();
    if (override && override_dir) {
        closedir(override_dir);
        LOGF("Got override: %s\n", override);
        LOGF("Current root dir: %s\n", assembly_dir);
        
        char bcl_root_full[PATH_MAX] = "\0";
        realpath(override, bcl_root_full);
        LOGF("New root path: %s\n", bcl_root_full);

        char *search_path;
        asprintf(&search_path, "%s:%s", bcl_root_full, assembly_dir);
        LOGF("Search path: %s\n", search_path);

        r_mono_set_assemblies_path(search_path);
        setenv("DOORSTOP_DLL_SEARCH_DIRS", search_path, 1);
        free(search_path);
    } else {
        LOG("No override (or failed to find), unsetting.\n");
        unsetenv("DOORSTOP_CORLIB_OVERRIDE_PATH");
        setenv("DOORSTOP_DLL_SEARCH_DIRS", assembly_dir, 1);
    }

    r_mono_config_parse(NULL);

    // Call the original r_mono_jit_init_version to initialize the Unity Root Domain
    void *domain = r_mono_jit_init_version(root_domain_name, runtime_version);

    if (getenv("DOORSTOP_INITIALIZED"))
    {
        LOG("DOORSTOP_INITIALIZED is set! Skipping!\n");
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

        LOGF("Setting config paths; basedir: %s; config: %s\n", folder_path, config_path);
        r_mono_domain_set_config(domain, folder_path, config_path);
    }

    char *dll_path = getenv("DOORSTOP_INVOKE_DLL_PATH");
    LOGF("DOORSTOP_INVOKE_DLL_PATH: %s\n", dll_path);

    LOGF("Managed dir: %s\n", assembly_dir);
    setenv("DOORSTOP_MANAGED_FOLDER_DIR", assembly_dir, TRUE);
    free(assembly_dir);

    char app_path[PATH_MAX] = "\0";
    program_path(app_path);
    setenv("DOORSTOP_PROCESS_PATH", app_path, TRUE);

    // Load our custom assembly into the domain
    void *assembly = r_mono_domain_assembly_open(domain, dll_path);

    if (assembly == NULL)
    {
        LOGF("Failed to load assembly '%s'\n", dll_path);
        return domain;
    }

    // Get assembly's image that contains CIL code
    void *image = r_mono_assembly_get_image(assembly);

    LOGF("Got image: %p \n", image);

    if (image == NULL)
    {
        LOG("Failed to locate the image!\n");
        return domain;
    }

    // Note: we use the runtime_invoke route since jit_exec will not work on DLLs

    // Create a descriptor for a random Main method
    void *desc = r_mono_method_desc_new("*:Main", FALSE);

    // Find the first possible Main method in the assembly
    void *method = r_mono_method_desc_search_in_image(desc, image);

    if (method == NULL)
    {
        LOGF("Failed to locate any entrypoints in '%s'!\n", dll_path);
        return domain;
    }
    else
    {
        LOGF("Found entrypoints in '%s'!\n", dll_path);
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

    LOG("Invoking entrypoint\n");

    void *exc;

    // invoke the main method of the target assembly
    r_mono_runtime_invoke(method, NULL, args, &exc);

    LOG("Invoking done!\n");

    if(exc)
    {
        // some managed exception occured in the target assembly.
        // try to print the exception.
        LOG("Exception thrown while invoking assembly entrypoint!\n");

        void *exc_str = r_mono_object_to_string(exc, NULL);
        if (exc_str)
        {
            char *s = r_mono_string_to_utf8(exc_str);
            LOGF("Exception: %s\n", s);
        }
        else
        {
            LOG("Unable to obtain exception text");
        }

        exit(1);
    }

    if (args != NULL)
    {
        free(args);
        args = NULL;
    }

    return domain;
}

void *hook_mono_image_open_from_data_with_name(void *data, uint32_t data_len, int need_copy, void *status, int refonly, char *name) {
    LOGF("Load DLL: %s\n", name);
    
    void *result = NULL;
    char *override = getenv("DOORSTOP_CORLIB_OVERRIDE_PATH");
    if (override) {
        char override_full[PATH_MAX] = "\0";
        realpath(override, override_full);

        char *filename = basename(name);
        LOGF("Base: %s\n", filename);

        char *new_path;
        asprintf(&new_path, "%s/%s", override_full, filename);

        if (access(new_path, F_OK) == 0) {
            LOGF("Redirecting to %s\n", new_path);
            FILE *f = fopen(new_path, "rb");
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            rewind(f);

            char *buf = malloc(sizeof(char) * size);
            fread(buf, size, 1, f);
            fclose(f);
            result = r_mono_image_open_from_data_with_name(buf, size, need_copy, status, refonly, name);
        }
        free(new_path);
    }
    if (!result) { result = r_mono_image_open_from_data_with_name(data, data_len, need_copy, status, refonly, name); }
    return result;
}

static char inited = 0;
void *dlsym_hook(void *handle, const char *name) {

    if (!strcmp(name, "mono_jit_init_version"))
    {
        if (!inited) {
            LOG("intercepting mono_jit_init_version\n");
            doorstop_init_mono_functions(handle);
            inited = 1;
        }
        return (void *)jit_init_hook;
    }
    else if (!strcmp(name, "mono_image_open_from_data_with_name"))
    {
        if (!inited)
        {
            LOG("intercepting mono_image_open_from_data_with_name\n");
            doorstop_init_mono_functions(handle);
            inited = 1;
        }
        return (void *)hook_mono_image_open_from_data_with_name;
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
    if (strcmp(getenv("DOORSTOP_ENABLE"), "TRUE"))
    {
        printf("[Doorstop] DOORSTOP_ENABLE is not TRUE! Disabling Doorstop...\n");
        return;
    }

    log_handle = fopen("doorstop.log", "w");

    if (log_handle == NULL)
    {
        printf("[Doorstop] unable to create logfile\n");
    }
    else
    {
        printf("[Doorstop] creating logfile 'doorstop.log'\n");
    }

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
        LOG("Found UnityPlayer, hooking into it instead\n");
    }
    else if(plthook_open(&hook, NULL) != 0) {
        LOGF("Failed to open current process PLT! Cannot run Doorstop! Error: %s\n", plthook_error());
        return;
    }

    if(plthook_replace(hook, "dlsym", &dlsym_hook, NULL) != 0)
        LOGF("Failed to hook dlsym, ignoring it. Error: %s\n", plthook_error());

    if(plthook_replace(hook, "fclose", &fclose_hook, NULL) != 0)
        LOGF("Failed to hook fclose, ignoring it. Error: %s\n", plthook_error());

#if __APPLE__
    /*
        On older Unity versions, Mono methods are resolved by the OS's loader directly.
        Because of this, there is no dlsym, in which case we need to apply a PLT hook.
    */
    void *mono_handle = plthook_handle_by_name("libmono");

    if(plthook_replace(hook, "mono_jit_init_version", &jit_init_hook, NULL) != 0)
        LOGF("Failed to hook jit_init_version, ignoring it. Error: %s\n", plthook_error());
    else if(mono_handle)
        doorstop_init_mono_functions(mono_handle);
#endif

    plthook_close(hook);
}