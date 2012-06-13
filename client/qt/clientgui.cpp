#include "clientgui.h"
#include <iostream>
#include "addcontactgui.h"

ClientGUI::ClientGUI(QMainWindow *parent) :
  QMainWindow(parent),
    _controller(*this)
{
   setupUi(this);
   QObject::connect(actionAddContact, SIGNAL(activated()), this, SLOT(createAddContact()));
}

ClientGUI::~ClientGUI() {
}

QString ClientGUI::getNewContactName() const
{
  if (!this->_addContact)
    return *new QString("");// TODO throw a fatal exception
  return this->_addContact->getNewContactName();
}

QString ClientGUI::getNewContactKey() const
{
  if (!this->_addContact)
    return *new QString("");// TODO throw a fatal exception
  return this->_addContact->getNewContactKey();
}

void ClientGUI::createAddContact() {
    _addContact = new addContactGui(this->_controller);

    //    QObject::connect(_addContact, SIGNAL(destroyed()), this, SLOT(deleteAddContact()));
    _addContact->show();
}

void ClientGUI::deleteAddContact() {
  delete _addContact;
  _addContact = 0;
}


void ClientGUI::addContact(const QString &str)
{
  this->ContactsList->addItem(str);
}

// void ClientGUI::addContact(Contact* c) {
//     QString str;
//     str.append(c->getName().c_str());
//     this->ContactsList->addItem(str);
// }
