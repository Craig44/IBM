import os
import sys
import os.path
import subprocess
import fileinput
import distutils

sys.path.insert(0, "buildtools/classes")

# These are CASAL2 specific python objects
from System import *
from Globals import *
from Builder import *
from Documentation import *
from ModelRunner import *

"""
Print the usage for this build system
"""
def print_usage():  
  os.system( [ 'clear', 'cls' ][ os.name == 'nt' ] )
  print '###########################################################'
  print '# IBM Build System Usage                               #'
  print '###########################################################'
  print 'Usage:'
  print 'doBuild <build_target> <build_parameter>'
  print ''
  print 'Valid Build Types:'
  print '  debug - Build standalone debug executable'
  print '  release - Build standalone release executable'
  print '  thirdparty - Build all required third party libraries'
  print '  thirdpartylean - Build minimal third party libraries'
  print '  modelrunner - Run the test suite of models'
  print '  clean - Remove any previous debug/release build information'
  print '  cleanall - Remove all previous build information'
  print '  check - Do a check of the build system'
  print '  IBM - Build the syntax section of the manual from source code'
  print ''
  print 'Valid Build Parameters: (thirdparty only)'
  print '  <libary name> - Target third party library to build or rebuild'
  print ''
  print ''
  return True


"""
Start our build system. This method essentially just
gets all of the system information we need and will
test that executables are in the path that we need
"""
def start_build_system():
  if Globals.operating_system_ == "windows":
    Globals.cmd_path_      = system_info.find_exe_path('cmd.exe')
    Globals.compiler_path_ = system_info.find_exe_path('g++.exe')
    Globals.git_path_      = system_info.find_exe_path('git.exe')
    Globals.r_path_        = system_info.find_exe_path('R.exe')
  else:
    Globals.compiler_path_ = system_info.find_exe_path('g++')
    Globals.git_path_      = system_info.find_exe_path('git')    
    if system_info.find_exe_path('unzip') == '':
      return Globals.PrintError('unzip is not in the current path. Please ensure it has been installed')
    if system_info.find_exe_path('cmake') == '':
      return Globals.PrintError('cmake is not in the current path. Please ensure it has been intalled')    
    if system_info.find_exe_path('R') == '':
      return Globals.PrintError('R is not in the current path. Please ensure it has been intalled')         
  system_info.set_new_path()

  if Globals.compiler_path_ == "":
    return Globals.PrintError("g++ is not in the current path")
  if Globals.git_path_ == "":
    return Globals.PrintError("git is not in the current path. Please install a git command line client (e.g http://git-scm.com/downloads)")  
  if Globals.operating_system_ == 'windows' and os.path.exists(Globals.git_path_ + '\\sh.exe'):
  	return Globals.PrintError("git version has sh.exe in the same location. This will conflict with cmake. Please upgrade to a 64bit version of Git")    
  if not system_info.find_gcc_version():
    return False

  # Check the compiler version to see if it's compatible
  pieces = Globals.compiler_version_.split('.')
  gcc_version = str(pieces[0]) + str(pieces[1])
  if gcc_version < '48':
  	return Globals.PrintError("G++ version " + Globals.compiler_version_ + " is not supported due to it's age")

  return True  

"""
Get the build information from the user
"""
def start():
  print '-- Checking for dateutil Python module'
  if 'dateutil' not in sys.modules:
    return Globals.PrintError("Python requires the module dateutil for the build system to work")
  print '-- Checking for datetime Python module'
  if 'datetime' not in sys.modules:
    return Globals.PrintError("Python requires the module datetime for the build system to work")
  print '-- Checking for re Python module'
  if 're' not in sys.modules:
    return Globals.PrintError("Python requires the module re for the build system to work")
  print '-- Checking for distutils Python module'
  if 'distutils' not in sys.modules:
    return Globals.PrintError("Python requires the module distutils for the build system to work")
  
  build_target = ""
  build_parameters = ""
  
  """
  Handle build information already passed in
  """
  if len(sys.argv) > 1 and len(str(sys.argv[1])) > 1:
      build_target = sys.argv[1]
  if len(sys.argv) > 2 and len(str(sys.argv[2])) > 1:
      build_parameters = sys.argv[2] 

  if build_target == "":
    return Globals.PrintError('Please provide a valid build target. Use doBuild help to see list');
  if not build_target.lower() in Globals.allowed_build_targets_:
    return Globals.PrintError(build_target + " is not a valid build target")
    
  build_target = build_target.lower()    
  if build_target == "help":
    print_usage()
    return True
  if build_target == "check":
	print "--> All checks completed successfully"
	return True 

  if build_parameters != "": 
    build_parameters = build_parameters.lower()
  
  Globals.build_target_ = build_target
  Globals.build_parameters_ = build_parameters
  
  print " -- Build target: " + Globals.build_target_
  print " -- Build parameters: " + Globals.build_parameters_
  print ""
  
  if build_target in Globals.allowed_build_types_:      
    if not build_parameters in Globals.allowed_build_parameters_:
      return Globals.PrintError("Build parameter " + build_parameters + " is not valid")
    
    print "*************************************************************************"
    print "*************************************************************************"
    print "--> Starting " + Globals.build_target_ + " Build"
    code_builder = MainCode()
    if not code_builder.start(False):
      return False
  if build_target == "thirdparty" or build_target == "thirdpartylean":
    print "*************************************************************************"
    print "*************************************************************************"
    print "--> Starting " + Globals.build_target_ + " Build"
    code_builder = ThirdPartyLibraries()
    if not code_builder.start():
      return False
  elif build_target == "clean":
    print "*************************************************************************"
    print "*************************************************************************"
    print "--> Cleaning all IBM built files"
    cleaner = Cleaner()
    if not cleaner.clean():
      return False
  elif build_target == "modelrunner":
    print "*************************************************************************"
    print "*************************************************************************"
    print "--> Starting " + Globals.build_target_ + " Build"
    model_runner = ModelRunner()
    if not model_runner.start():
      return False      
  elif build_target == "documentation":
    print "*************************************************************************"
    print "*************************************************************************"
    print "--> Starting " + Globals.build_target_ + " Build"
    documentation_builder = Documentation()
    if not documentation_builder.start():
      return False	  
  elif build_target == "cleanall":
    print "*************************************************************************"
    print "*************************************************************************"
    print "--> Cleaning all IBM built files, including third party headers and libs"
    cleaner = Cleaner()
    if not cleaner.clean_all():
      return False

  return True
  
"""
This is the entry point in to our build system
"""
system_info = SystemInfo()
if not start_build_system():
  system_info.reset_original_path()
  exit(1)
  
exit_code = 0
if not start():
  exit_code = 1
  
system_info.reset_original_path()
print "--> Finished "
exit(exit_code)  
