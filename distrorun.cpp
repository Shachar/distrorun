#include "config.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

#include <getopt.h>
#include <libconfig.h++>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <string.h>

bool sanitize_extra_volume(std::filesystem::path volume);

struct Parameters {
    std::string name;
    std::vector<std::filesystem::path> extraVolumes;
    std::filesystem::path workDir;
    int argc;
    char **argv;
    char **env;
};

int container_code(void *void_params) {
    auto params = reinterpret_cast<const Parameters *>(void_params);

    // Drop all privileges before executing
    setuid( getuid() );
    execvpe( params->argv[0], params->argv, params->env );
    std::cerr<<"Couldn't execute command: "<<strerror(errno)<<"\n";

    return 5;
}

Parameters parse_params(int argc, char *argv[]) {
    Parameters params;

    {
        char *wd = get_current_dir_name();
        params.workDir = wd;
        free(wd);
    }

    enum Options { Done=-1, ExtraVolumes };
    const struct option longopts[] = {
        {
            .name = "extra-volume",
            .has_arg = required_argument,
            .flag = nullptr,
            .val = Options::ExtraVolumes,
        },
        {}
    };
    Options opt;

    while( (opt = Options( getopt_long(argc, argv, "", &longopts[0], nullptr))) != Options::Done ) {
        switch( opt ) {
        case Options::ExtraVolumes: params.extraVolumes.emplace_back(optarg); break;
        case Options::Done: abort();
        }
    }

    if( optind>=argc ) {
        std::cerr<<"Must get container name as argument\n";

        exit(2);
    }

    // SECURITY: make sure the container name argument is not a path, or the user can redirect it to an unsecure file.
    if( strchr( argv[optind], '/' ) ) {
        std::cerr<<"Container name may not contain the '/' character\n";

        exit(2);
    }

    params.name = argv[optind];
    optind++;
    params.argc = argc-optind;
    params.argv = argv+optind;

    return params;
}

int main(int argc, char *argv[], char *env[]) {
#if 0
    if( getuid() == geteuid() && getuid()!=0 ) {
        std::cerr<<"This program needs to be SUID to function\n";

        return 1;
    }
#endif

    Parameters params = parse_params(argc, argv);

    std::filesystem::path config_file( SYSCONFDIR );
    config_file /= PACKAGE_NAME ".d";
    config_file /= params.name + ".conf";

    libconfig::Config cfg;
    
    try {
        cfg.readFile(config_file.c_str());
    } catch(libconfig::FileIOException &ex) {
        std::cerr << "Error while reading config file "<<config_file<<": "<<ex.what()<<"\n";

        return 1;
    } catch(libconfig::ParseException &ex) {
        std::cerr << "Error while parsing config file "<<ex.getFile()<<":"<<ex.getLine()<<": "<<ex.getError()<<"\n";

        return 1;
    }

    // Make sure user has access to all command line passed volumes
    seteuid( getuid() );        // Saved UID will allow us to return to root
    for( auto volume : params.extraVolumes ) {
        if( !sanitize_extra_volume(volume) )
            return 1;
    }

    if( seteuid(0) ) {
        std::cerr<<"Failed to restore privilige\n";

        return 2;
    }

    static constexpr size_t STACK_SIZE = 16384;
    void *child_stack = mmap( NULL, STACK_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_GROWSDOWN|MAP_STACK, -1, 0);
    if( child_stack==MAP_FAILED ) {
        std::cerr<<"Failed to allocate stack: "<<strerror(errno)<<"\n";

        return 2;
    }

    params.env = env;
    int ret = clone(
            container_code,
            child_stack + STACK_SIZE,            // Different address space, use parent's stack
            CLONE_NEWNS|SIGCHLD,
            &params);

    if( ret==-1 ) {
        std::cerr<<"Failed to launch child process: "<<strerror(errno)<<"\n";

        return 3;
    }

    int status;
    ret = wait(&status);
    if( ret==-1 ) {
        std::cerr<<"Parent wait on child failed: "<<strerror(errno)<<"\n";

        return 3;
    }

    if( WIFEXITED(status) )
        return WEXITSTATUS(status);

    if( WIFSIGNALED(status) ) {
        std::cerr<<"Child exit with signal "<<WTERMSIG(status)<<"\n";

        return 3;
    }

    std::cerr<<"Child exit with unknown reason "<<status<<"\n";

    return 4;
}

bool sanitize_extra_volume(std::filesystem::path volume) { 
    struct stat status;
    int ret = ::lstat( volume.c_str(), &status );
    if( ret!=0 ) {
        std::cerr<<"Failed to access "<<volume<<": "<<strerror(errno)<<"\n";

        return false;
    }
    if( (status.st_mode & S_IFMT) != S_IFDIR ) {
        std::cerr<<"Error: "<<volume<<" is not a directory\n";

        return false;
    }

    return true;
}
