//
//  SSH_functions.m
//  MDMPatcher Remastered
//
//  Created by Jan Fabel on 08.04.21.
//

#import <Foundation/Foundation.h>
#import <NMSSH/NMSSH.h>
#include "SSH_functions.h"

int eraseSSH ()
{
      NSLog (@"Process started...");
    NMSSHSession *session = [NMSSHSession connectToHost:@"127.0.0.1:2222"
                                           withUsername:@"root"];
    

    if (session.isConnected) {
        [session authenticateByPassword:@"alpine"];

        if (session.isAuthorized) {
            NSLog(@"Authentication succeeded");
        }
    } else {
        return @"ERROR";
    }
        
    NSError *error = nil;
    [session.channel execute:@"mount -o rw,union,update /" error:&error];
    [session.channel execute:@"plutil -replace IsMDMUnremovable -float 0 /var/containers/Shared/SystemGroup/systemgroup.com.apple.configurationprofiles/Library/ConfigurationProfiles/CloudConfigurationDetails.plist" error:&error];
    [session.channel execute:@"plutil -IsMandatory -0 /var/containers/Shared/SystemGroup/systemgroup.com.apple.configurationprofiles/Library/ConfigurationProfiles/CloudConfigurationDetails.plist" error:&error];
    [session.channel execute:@"plutil -IsSupervised -0 /var/containers/Shared/SystemGroup/systemgroup.com.apple.configurationprofiles/Library/ConfigurationProfiles/CloudConfigurationDetails.plist" error:&error];
    [session.channel execute:@"plutil -replace IsMDMUnremovable -float 0 /var/containers/Shared/SystemGroup/systemgroup.com.apple.configurationprofiles/Library/ConfigurationProfiles/CloudConfigurationSetAsideDetails.plist" error:&error];
    [session.channel execute:@"plutil -IsMandatory -0 /var/containers/Shared/SystemGroup/systemgroup.com.apple.configurationprofiles/Library/ConfigurationProfiles/CloudConfigurationSetAsideDetails.plist" error:&error];
    [session.channel execute:@"plutil -IsSupervised -0 /var/containers/Shared/SystemGroup/systemgroup.com.apple.configurationprofiles/Library/ConfigurationProfiles/CloudConfigurationSetAsideDetails.plist" error:&error];
    [session.channel execute:@"killall -9 SpringBoard" error:&error];

    sleep(1);
    [session disconnect];
    return 0;
}
