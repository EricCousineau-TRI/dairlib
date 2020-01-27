# Iiwa Controller Examples
Contains a Kuka LBR Iiwa simulation using MultibodyPlant (updated from Drake's RigidbodyPlant sim)
and a controller that moves the Iiwa endeffector around.

## Running simulation test

1. After building dairlib, from the dairlib root directory run `bot-procman-sheriff -l /examples/kuka_iiwa_arm/kuka_test.pmd`. A new window should open.
2. Start `operator: drake-director` by expanding the "Operator" list and right clicking `drake-director`. A new window should open.
3. Start `simulated-robot: simulator`. A robot should appear in the drake-director window.
4. Start `simulated-robot: position _broadcaster`. The robot should start moving.
5. To restart a test, stop and then start the `simulator` and `position_broadcaster`.

To change the path taken by the robot, modify the file `Trajectories.csv` (in the `kuka_iiwa_arm` directory). This file is formated as a list of waypoints, where each row is the time and position `(t,x,y,z)`.


## Configuration Files
* Trajectories.csv
* EndEffectorOrientations.csv
* simulationsettings.json

## Using `gitcheck.py`

gitcheck.py's purpose is to automate various pieces of the documentation process for the KUKA experiments. Specifically, gitcheck checks the local repository's git status, runs lcm logger, uploads the kuka settings json file, the trajectories csv file, and the newly created lcm file to the shared folder, and updates the Test Log Files Google Spreadsheet.

### Prerequisites:
1. The Google Client library must be installed.
2. gitcheck.py must be run with python version 3 or above.

### Additional Notes:
* Lines 79-92: gitcheck.py implements the Google Drive API to both upload and edit files. Accordingly, this file must go through the Google Drive authentication procedure. The first three linse of this code check to see if a token file, which stores the user's acces information, exists already. If it finds the file, Google knows gitcheck.py has already been authorized, and it skips lines 82-92. If not, it will either try to refresh the codes's credentials (Lines 84-85) or it will request permission from the file user through a Google pop-up window. When this occurs, it is important to sign into an account that has access to the shared Log Documentation folder. Google may also present a warning screen, but this may be circumvented by clicking on Advanced settings. Once this process is completed, gitcheck will authomatically execute lcm-logger.

* Lines 119-120: These lines initalize date and time strings to be used as a time stamps on each file. Their formats may be changed relatively easily using the default python strftime format.