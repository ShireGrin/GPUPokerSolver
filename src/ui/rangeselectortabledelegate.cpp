#include "include/ui/rangeselectortabledelegate.h"

RangeSelectorTableDelegate::RangeSelectorTableDelegate(QStringList ranks,RangeSelectorTableModel *rangeSelectorTableModel,QObject *parent):WordItemDelegate(parent){
    this->rank_list = ranks;
    this->rangeSelectorTableModel = rangeSelectorTableModel;
}

void RangeSelectorTableDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const{

    painter->save();
    auto options = option;
    initStyleOption(&options, index);

    QRect rect(option.rect.left(), option.rect.top(),\
             option.rect.width(), option.rect.height());
    QBrush brush(Qt::gray);
    if(index.column() == index.row())brush = QBrush(Qt::darkGray);
    painter->fillRect(rect, brush);

    float range_float = this->rangeSelectorTableModel->getRangeAt(index.row(),index.column());

    if (range_float > 0.0f) {
        QColor yellowColor = Qt::yellow;
        yellowColor.setAlphaF(range_float);
        brush = QBrush(yellowColor);
        painter->fillRect(rect, brush);
    }

    QTextDocument doc;
    doc.setHtml(options.text);

    painter->translate(options.rect.left(), options.rect.top());
    QRect clip(0, 0, options.rect.width(), options.rect.height());
    if(!this->rangeSelectorTableModel->in_thumbnail_mode()){
        doc.drawContents(painter, clip);
        if (range_float > 0.0f) {
            QFont f = painter->font();
            f.setPointSize(7);
            painter->setFont(f);
            painter->setPen(Qt::black);
            QString pct = QString::number((int)(range_float * 100)) + "%";
            painter->drawText(clip, Qt::AlignBottom | Qt::AlignRight, pct);
            
            float count_val = this->rangeSelectorTableModel->getCountAt(index.row(), index.column());
            if (count_val > 0.001f) {
                QString count_str = QString(" %1").arg(QString::number(count_val, 'f', 0));
                painter->setPen(Qt::black);
                painter->drawText(clip, Qt::AlignBottom | Qt::AlignLeft, count_str);
            }
        }
    }
    painter->restore();
}
