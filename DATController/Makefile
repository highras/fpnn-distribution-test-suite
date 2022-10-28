dirs = Prototype DATStatus DATMachineStatus DATActorUploader DATDeployController DATAction

all:
	for x in $(dirs); do (cd $$x; make) || exit 1; done

clean:
	for x in $(dirs); do (cd $$x; make clean) || exit 1; done