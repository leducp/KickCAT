#### Generate OD dictionary based on ESI file
If your application uses mailboxes with SDO, you might want to populate your Object Dictionary based on an ESI file. To do so you will find in tools folder an "od_generator".
To use it : 
    `./od_generator [Your ESI file]`
It will create an od_populator.cc file that you can build with your application and call the function populateOD();

NB: You can also write your own od_populator.cc (by implementing the populatedOD function) if you want to.