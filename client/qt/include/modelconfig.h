/*
 * Copyright (c) 2012 Florent Tribouilloy <tribou_f AT epitech DOT net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef MODELCONFIG_H_
# define MODELCONFIG_H_

#include "imodel.h"

class Controller;

class ModelConfig : public IModel
{
public:
  ModelConfig(Controller &);
  virtual ~ModelConfig() {}

  virtual const QString &getObjectName() const;
  virtual const QMap<QString, QVariant>* getData() const;
  virtual void feedData(const QString &, const QVariant&);

private:
  static const QString _name;
  QMap<QString, QVariant>* _config;
  Controller    &_controller;
};

#endif /* !MODELCONFIG_H_ */
