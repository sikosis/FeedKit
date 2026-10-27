#ifndef TAPPLICATION_H
#define TAPPLICATION_H

#include <Application.h>

class TWindow;

class TApplication : public BApplication {
	public:
							TApplication(bool smokeTest = false);

		void				ReadyToRun();

	private:
		bool				fSmokeTest;
		TWindow				*fWindow;
};

#endif
