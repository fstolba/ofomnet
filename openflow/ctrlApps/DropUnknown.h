/*
 * Hub.h
 *
 *  Created on: 01.11.2017
 *      Author: dbelsf
 */

#ifndef DROPUNKNOWN_H_
#define DROPUNKNOWN_H_

#include "OFA_controller.h"

class Drop: public cSimpleModule, public cListener
{
public:
    Drop();
    ~Drop();
    void receiveSignal(cComponent *src, simsignal_t id, cObject *obj, cObject *details);

protected:
    virtual void initialize();


private:
    OFA_controller *controller;
};


#endif /* DROPUNKNOWN_H_ */
