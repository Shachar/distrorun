#include "config.h"

#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

#include <getopt.h>
#include <libconfig.h++>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <string.h>

struct Parameters {
    std::string name;
    std::filesystem::path root;
    std::vector<std::filesystem::path> volumes, extraVolumes;
    std::filesystem::path workDir;
    int argc;
    char **argv;
    char **env;
};

#define invoke( func, errmsg, returncode ) \
        if( (func) != 0 ) { std::cerr<<errmsg<<": "<<strerror(errno)<<"\n"; return (returncode); }

bool sanitize_extra_volume(std::filesystem::path volume);
void parse_config(Parameters &params);

int container_code(const Parameters &params) {

    std::cerr<<"Root is "<<params.root<<"\n";
    invoke( mount( "none", "/", nullptr, MS_REC|MS_PRIVATE, nullptr ), "Remount of root failed", 5 );

    for( const auto &volume : params.volumes ) {
        std::filesystem::path mountpoint = params.root;
        mountpoint /= volume.relative_path();
        std::cerr<<"Mounting "<<volume<<" on "<<mountpoint<<"\n";
        invoke( mount( volume.c_str(), mountpoint.c_str(), nullptr, MS_BIND|MS_REC|MS_PRIVATE, nullptr ), "Failed to mount "<<volume, 5 );
    }

    for( const auto &volume : params.extraVolumes ) {
        std::filesystem::path mountpoint = params.root;
        mountpoint /= volume.relative_path();
        std::cerr<<"Mounting "<<volume<<" on "<<mountpoint<<"\n";
        invoke( mount( volume.c_str(), mountpoint.c_str(), nullptr, MS_BIND|MS_REC|MS_PRIVATE, nullptr ), "Failed to mount "<<volume, 5 );
    }

    invoke( chroot(params.root.c_str()), "Failed to chroot to "<<params.root, 5 );
    invoke( chdir("/"), "Failed to chdir to new root", 5 );
    invoke( mount("proc", "/proc", "proc", 0, nullptr), "Failed to mount /proc", 5 );
    invoke( mount("none", "/sys", "sysfs", 0, nullptr), "Failed to mount /sys", 5 );

    // Drop all privileges before executing
    invoke( setuid( getuid() ), "Failed to drop priviliges", 5 );

    invoke( chdir(params.workDir.c_str()), "Couldn't chdir to "<<params.workDir, 5 );

    execvpe( params.argv[0], params.argv, params.env );
    std::cerr<<"Couldn't execute command: "<<strerror(errno)<<"\n";

    return 5;
}

Parameters parse_params(int argc, char *argv[]) {
    std::optional<std::string> previousPosixCorrect;
    {
        const char *var = getenv("POSIXLY_CORRECT");
        if( var )
            previousPosixCorrect.emplace(var);
    }

    setenv("POSIXLY_CORRECT", "1", true);

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

    if( previousPosixCorrect ) {
        setenv("POSIXLY_CORRECT", previousPosixCorrect->c_str(), true);
    } else {
        unsetenv("POSIXLY_CORRECT");
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
    if( getuid() == geteuid() && getuid()!=0 ) {
        std::cerr<<"This program needs to be SUID to function\n";

        return 1;
    }

    Parameters params = parse_params(argc, argv);

    parse_config(params);

    // Make sure user has access to all command line passed volumes
    invoke( seteuid( getuid() ), "Failed to temporarily drop privileges", 3 );  // Saved UID will allow us to return to root
    for( auto volume : params.extraVolumes ) {
        if( !sanitize_extra_volume(volume) )
            return 1;
    }

    invoke( seteuid(0), "Failed to restore priviliges", 2 );

    params.env = env;

    unshare(CLONE_FS|CLONE_NEWNS);
    return container_code(params);
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


void parse_config(Parameters &params) {
    std::filesystem::path config_file( SYSCONFDIR );
    config_file /= PACKAGE_NAME ".d";
    config_file /= params.name + ".conf";

    libconfig::Config cfg;
    try {
        cfg.readFile(config_file.c_str());
    } catch(libconfig::FileIOException &ex) {
        std::cerr << "Error while reading config file "<<config_file<<": "<<ex.what()<<"\n";

        exit(1);
    } catch(libconfig::ParseException &ex) {
        std::cerr << "Error while parsing config file "<<ex.getFile()<<":"<<ex.getLine()<<": "<<ex.getError()<<"\n";

        exit(1);
    }

    const libconfig::Setting &cfgRoot = cfg.getRoot();

    params.root = cfgRoot["dir"];
    for( const char *volume : cfgRoot["mapped_volumes"] ) {
        if( volume[0]!='/' ) {
            std::cerr<<"Mapped volume "<<volume<<" is a relative path\n";

            exit(1);
        }
        params.volumes.emplace_back( volume );
    }

    // Cannonize the work directory
    auto relativeWd = params.workDir.lexically_relative(params.root);
    if( relativeWd.string().find("../")!=0 && relativeWd.string()!=".." && relativeWd.c_str()[0]!='/' ) {
        std::cout<<"Mapping work dir from "<<params.workDir<<" to "<<relativeWd<<"\n";
        // workDir is a subdirectory or root
        params.workDir = relativeWd;
    }
}
