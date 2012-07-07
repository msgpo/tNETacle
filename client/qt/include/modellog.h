#ifndef MODELLOG_H_
# define MODELLOG_H_

#include "imodel.h"

class Controller;

class ModelLog : public IModel
{
public:
  ModelLog(Controller &);
  virtual const QString &getObjectName() const;
  virtual const QMap<QString, QString>* getData() const;
  virtual void feedData(const QString &, const QMap<QString, QVariant> &);
  static const QString _name;
  Controller    &_controller;
};

#endif /* !MODELLOG_H_ */
