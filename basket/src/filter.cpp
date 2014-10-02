/***************************************************************************
 *   Copyright (C) 2003 by Sébastien Laoût                                 *
 *   slaout@linux62.org                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "filter.h"

#include <QToolButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QPixmap>
#include <QLineEdit>
#include <QComboBox>
#include <QIcon>

#include "global.h"
#include "tools.h"
#include "tag.h"
#include "bnpview.h"
#include "focusedwidgets.h"

/** FilterBar */

FilterBar::FilterBar(QWidget *parent)
        : QWidget(parent)/*, m_blinkTimer(this), m_blinkedTimes(0)*/
{
    QHBoxLayout *hBox  = new QHBoxLayout(this);

    // Create every widgets:
    // (Aaron Seigo says we don't need to worry about the
    //  "Toolbar group" stuff anymore.)

    m_resetButton = new QToolButton(this);
    m_resetButton->setIcon(QIcon::fromTheme("dialog-close"));
    m_resetButton->setText(tr("Reset Filter"));//, /*groupText=*/"", this, SLOT(reset()), 0);
    m_resetButton->setAutoRaise(true);
    //new KToolBarButton("locationbar_erase", /*id=*/1230, this, /*name=*/0, tr("Reset Filter"));
    m_lineEdit = new QLineEdit(this);
    QLabel *label = new QLabel(this);
    label->setText(tr("&Filter: "));
    label->setBuddy(m_lineEdit);
    m_tagsBox = new QComboBox(this);
    QLabel *label2 = new QLabel(this);
    label2->setText(tr("T&ag: "));
    label2->setBuddy(m_tagsBox);
    m_inAllBasketsButton = new QToolButton(this);
    m_inAllBasketsButton->setIcon(QIcon::fromTheme("edit-find"));
    m_inAllBasketsButton->setText(tr("Filter All Baskets"));//, /*groupText=*/"", this, SLOT(inAllBaskets()), 0);
    m_inAllBasketsButton->setAutoRaise(true);

    // Configure the Tags combobox:
    repopulateTagsCombo();

    // Configure the Search in all Baskets button:
    m_inAllBasketsButton->setCheckable(true);
//  m_inAllBasketsButton->setChecked(true);
//  Global::bnpView->toggleFilterAllBaskets(true);

    // Layout all those widgets:
    hBox->addWidget(m_resetButton);
    hBox->addWidget(label);
    hBox->addWidget(m_lineEdit);
    hBox->addWidget(label2);
    hBox->addWidget(m_tagsBox);
    hBox->addWidget(m_inAllBasketsButton);

    m_data = new FilterData(); // TODO: Not a pointer! and return a const &  !!

//  connect( &m_blinkTimer,         SIGNAL(timeout()),                   this, SLOT(blinkBar())                  );
    connect(m_resetButton,        SIGNAL(clicked()),                   this, SLOT(reset()));
    connect(m_lineEdit,           SIGNAL(textChanged(const QString&)), this, SLOT(changeFilter()));
    connect(m_tagsBox,            SIGNAL(activated(int)),              this, SLOT(tagChanged(int)));

//  connect(  m_inAllBasketsButton, SIGNAL(clicked()),                   this, SLOT(inAllBaskets())              );
    connect(m_inAllBasketsButton, SIGNAL(toggled(bool)), Global::bnpView, SLOT(toggleFilterAllBaskets(bool)));

    FocusWidgetFilter *lineEditF = new FocusWidgetFilter(m_lineEdit);
    m_tagsBox->installEventFilter(lineEditF);
    connect(lineEditF, SIGNAL(escapePressed()), SLOT(reset()));
    connect(lineEditF, SIGNAL(returnPressed()), SLOT(changeFilter()));
}

FilterBar::~FilterBar()
{
}

void FilterBar::setFilterAll(bool filterAll)
{
    m_inAllBasketsButton->setChecked(filterAll);
}

void FilterBar::setFilterData(const FilterData &data)
{
    m_lineEdit->setText(data.string);

    int index = 0;
    switch (data.tagFilterType) {
    default:
    case FilterData::DontCareTagsFilter: index = 0; break;
    case FilterData::NotTaggedFilter:    index = 1; break;
    case FilterData::TaggedFilter:       index = 2; break;
    case FilterData::TagFilter:          filterTag(data.tag);     return;
    case FilterData::StateFilter:        filterState(data.state); return;
    }

    if (m_tagsBox->currentIndex() != index) {
        m_tagsBox->setCurrentIndex(index);
        tagChanged(index);
    }
}

void FilterBar::repopulateTagsCombo()
{
    static const int ICON_SIZE = 16;

    m_tagsBox->clear();
    m_tagsMap.clear();
    m_statesMap.clear();

    m_tagsBox->addItem("");
    m_tagsBox->addItem(tr("(Not tagged)"));
    m_tagsBox->addItem(tr("(Tagged)"));

    int index = 3;
    TagModelItem     *tag;
    TagModelItem     *state;
    QIcon  icon;
    QString  text;
    QPixmap  emblem;
    for (int row = 0; row < Global::tagModel->rowCount(); ++row) {
        QModelIndex tagIndex = Global::tagModel->index(row, 0);
        tag   = Global::tagModel->getItem(tagIndex);
        state = tag->child(0);
        // Insert the tag in the combo-box:
        if (tag->childCount() > 1)
            text = tag->data();
        else
            text = state->data();

        icon = QIcon("://tags/hi16-action-" + state->emblem() + ".png");
        emblem = icon.pixmap(ICON_SIZE, ICON_SIZE);

        m_tagsBox->insertItem(index, emblem, text);
        // Update the mapping:
        m_tagsMap.insert(index, tag);
        ++index;

        if (tag->childCount() > 1) {
            for (int subrow = 0; subrow < Global::tagModel->rowCount(tagIndex); ++subrow) {
                QModelIndex stateIndex;
                stateIndex = Global::tagModel->index(subrow, 0, tagIndex);
                state = Global::tagModel->getItem(stateIndex);
                // Insert the state:
                text = state->data();
                icon = QIcon("://tags/hi16-action-" + state->emblem() + ".png");
                emblem = icon.pixmap(ICON_SIZE, ICON_SIZE);
                // Indent the emblem to show the hierarchy relation:
                if (!emblem.isNull())
                    emblem = Tools::indentPixmap(emblem, /*depth=*/1, /*deltaX=*/2 * ICON_SIZE / 3);
                m_tagsBox->insertItem(index, emblem, text);
                // Update the mapping:
                m_statesMap.insert(index, state);
                ++index;
            }
        }
    }
}

void FilterBar::reset()
{
    m_lineEdit->setText(""); // m_data->isFiltering will be set to false;
    m_lineEdit->clearFocus();
    changeFilter();
    if (m_tagsBox->currentIndex() != 0) {
        m_tagsBox->setCurrentIndex(0);
        tagChanged(0);
    }
    hide();
    emit newFilter(*m_data);
}

void FilterBar::filterTag(TagModelItem *tag)
{
    int index = 0;

    for (QMap<int, TagModelItem*>::Iterator it = m_tagsMap.begin(); it != m_tagsMap.end(); ++it)
        if (it.value() == tag) {
            index = it.key();
            break;
        }
    if (index <= 0)
        return;

    if (m_tagsBox->currentIndex() != index) {
        m_tagsBox->setCurrentIndex(index);
        tagChanged(index);
    }
}

void FilterBar::filterState(TagModelItem *state)
{
    int index = 0;

    for (QMap<int, TagModelItem*>::Iterator it = m_statesMap.begin(); it != m_statesMap.end(); ++it)
        if (it.value() == state) {
            index = it.key();
            break;
        }
    if (index <= 0)
        return;

    if (m_tagsBox->currentIndex() != index) {
        m_tagsBox->setCurrentIndex(index);
        tagChanged(index);
    }
}

void FilterBar::inAllBaskets()
{
    // TODO!
}

void FilterBar::setEditFocus()
{
    m_lineEdit->setFocus();
}

bool FilterBar::hasEditFocus()
{
    return m_lineEdit->hasFocus() || m_tagsBox->hasFocus();
}

const FilterData& FilterBar::filterData()
{
    return *m_data;
}

void FilterBar::changeFilter()
{
    m_data->string = m_lineEdit->text();
    m_data->isFiltering = (!m_data->string.isEmpty() || m_data->tagFilterType != FilterData::DontCareTagsFilter);
    if (hasEditFocus())
        m_data->isFiltering = true;
    emit newFilter(*m_data);
}

void FilterBar::tagChanged(int index)
{
    m_data->tag   = 0;
    m_data->state = 0;
    switch (index) {
    case 0:
        m_data->tagFilterType = FilterData::DontCareTagsFilter;
        break;
    case 1:
        m_data->tagFilterType = FilterData::NotTaggedFilter;
        break;
    case 2:
        m_data->tagFilterType = FilterData::TaggedFilter;
        break;
    default:
        // Try to find if we are filtering a tag:
        QMap<int, TagModelItem*>::iterator it = m_tagsMap.find(index);
        if (it != m_tagsMap.end()) {
            m_data->tagFilterType = FilterData::TagFilter;
            m_data->tag           = *it;
        } else {
            // If not, try to find if we are filtering a state:
            QMap<int, TagModelItem*>::iterator it2 = m_statesMap.find(index);
            if (it2 != m_statesMap.end()) {
                m_data->tagFilterType = FilterData::StateFilter;
                m_data->state         = *it2;
            } else {
                // If not (should never happens), do as if the tags filter was reseted:
                m_data->tagFilterType = FilterData::DontCareTagsFilter;
            }
        }
        break;
    }
    m_data->isFiltering = (!m_data->string.isEmpty() || m_data->tagFilterType != FilterData::DontCareTagsFilter);
    if (hasEditFocus())
        m_data->isFiltering = true;
    emit newFilter(*m_data);
}
