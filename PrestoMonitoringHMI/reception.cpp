#include "reception.h"
#include <QHostAddress>

/**
 * @brief Reception::Reception
 * @param parent
 */
Reception::Reception(QObject *parent) :
    QThread(parent)
{


}

/**
 * @brief Reception::connexion
 */
void Reception::connexion()
{
    unsigned short port = m_portValue;
    udpSocket4.bind(QHostAddress::AnyIPv4, port, QUdpSocket::ShareAddress);
    udpSocket4.joinMulticastGroup(groupAdress4);
    connect(&dataTimer, SIGNAL(timeout()), this, SLOT(readBruteMessage()));
    dataTimer.start(100); // Every 100 ms

}

/**
 * @brief Reception::disconnect
 */
void Reception::disconnect()
{
    udpSocket4.disconnectFromHost();
    dataTimer.stop();
}

/**********************************************************************************
// Lecture brute du message, préparation pour le traitement
**********************************************************************************/
/**
 * @brief Reception::readBruteMessage
 */
void Reception::readBruteMessage()
{
    static QByteArray datagram;
    list<PlatformComMessage>   m_pfComList;
    static int nbMessageCounter = 0;

    // Tant qu'il y a des données en attentes
    while (udpSocket4.hasPendingDatagrams())
    {
        // init de l'objet message PF Com
        PlatformComMessage pfComMsg;

        datagram.resize(int(udpSocket4.pendingDatagramSize()));
        udpSocket4.readDatagram(datagram.data(), datagram.size());

        // Initailisation
        string header = datagram.toStdString().substr(0,6);
        string message = datagram.toStdString().substr(6,*((short *)&header[4])-6);
        short sizePF = qToBigEndian(*((short *)&header[4]))-6;

        // Remplissage de la list PF COM
        pfComMsg.setDatagram(message);
        pfComMsg.setDatagramSize(sizePF);
        m_pfComList.push_back(pfComMsg);

    }
    // 1nombre de messages reçus, envoyé vers la view
    nbMessageCounter += m_pfComList.size();
    emit sendNbMsgToView(nbMessageCounter);
    // Si PF COM msg list  non vide, on envoie
    if (m_pfComList.size() > 0) emit sendPFcomData(m_pfComList);
}

/**
 * @brief Reception::on_timeout
 */
void Reception::on_timeout()
{
    // A chaque time_out, on va lire le socket UDP (remplacement du ReadyRead)
    readBruteMessage();
}



/**
 * @brief Reception::updateIPvalue
 * @param arg1
 */
void Reception::updateIPvalue(const QString &arg1)
{
    m_ipValue = arg1;
    // On set l'ip au QhostAdress
    groupAdress4.setAddress(arg1);
}

/**
 * @brief Reception::updatePortvalue
 * @param arg2
 */
void Reception::updatePortvalue(const QString &arg2)
{
    m_portValue = arg2.toUShort();
}

/**
 * @brief Reception::updateConnexionvalue
 * @param p_ipValue
 * @param p_portValue
 */
void Reception::updateConnexionvalue(QString p_ipValue, unsigned short p_portValue)
{
    m_ipValue = p_ipValue;
    groupAdress4.setAddress(m_ipValue);
    m_portValue = p_portValue;
}


