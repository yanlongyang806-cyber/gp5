These files exist to do automated builds without an attached Xbox, and without a separate build
configuration in the project files. When an FC build is done, XboxNoDeployOverride should be copied
over the XboxNoDeploy.vsprops file, which disables Xbox deployment. When finished, XboxNoDeployNoOverride
should be copied back over it.

XboxNoDeployNoOverride and the default XboxNoDeploy property sheets should be the same, i.e. they
should deploy the binaries to the Xbox.

There are also files for enabling the linker optimizations we desire on the builds we send out to customers
(disabling Edit and Continue, etc).  They are named with the same scheme.
