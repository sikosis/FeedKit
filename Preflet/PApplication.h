#ifndef PAPPLICATION_H
#define PAPPLICATION_H

#include <Application.h>

class PWindow;

class PApplication : public BApplication {
	public:
	
							PApplication(bool smokeTest = false);
							~PApplication(void);
		
		// BApplication Hooks				
		bool				QuitRequested(void);
		void				MessageReceived(BMessage *msg);
		void				ReadyToRun(void);
				
	private:
		PWindow				*fWindow;
		bool				fSmokeTest;
};

#endif

